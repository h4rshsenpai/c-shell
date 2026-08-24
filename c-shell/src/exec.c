#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>  // pipe() requires this
#include <sys/types.h>
#include <sys/wait.h>

#include "parser.h"     // Command struct
#include "exec.h"
#include "builtins.h"

// POSIX allows write() to write fewer bytes than requested 
// Helper function for worker_feed & worker_consume to handle partial writes by looping till count bytes are written
static int write_all(int fd, const char* buffer, size_t count) {
    
    size_t written = 0;
    while (written < count) {
        ssize_t n = write(fd, buffer + written, count - written);
        if (n < 0) {
            perror("cshell: helper function for write failed during redirection");
            return 1;
        }
        written += (size_t)n;
    }    
    return 0;
}

static void execute_pipeline(Command *head);
static void execute_single_command(Command *cmd, int prev_fd, int next_fd, pid_t *child_pid);

static int setup_redirects_input(int *pipefd, int **input_fds, const int n_files, char *const *files) {

    if (!n_files) return 0; // do nothing if no input files
    
    // 1. open all files in read-only mode first, return immediately if a file doesn't exist 
    
    *input_fds = malloc(n_files*sizeof(int)); 
    if (! *input_fds) {
        perror("cshell: malloc failed during input redirection");
        return 2;
    }    

    for (int i = 0; i < n_files; i++) {           
        (*input_fds)[i] = open(files[i], O_RDONLY);

        if ((*input_fds)[i] == -1) {     // open() failed; assignment uses one shell message for any input-open failure
            // close already open file descriptors before returning; free malloc'd pointer 

            while(i > 0) close((*input_fds)[--i]); 
            free(*input_fds); *input_fds = NULL;                    
            return 1; 
        }
    }

    // 2. create a pipe that will become the command's stdin

    if (pipe(pipefd) == -1) { 
        perror("cshell: pipe() failed during input redirection"); 
        
        // close all file descriptors before returning; free malloc'd pointer
        for (int i = 0; i < n_files ; i++ ) close((*input_fds)[i]); 
        free(*input_fds); *input_fds = NULL;
        return 2; 
    }

    return 0;
}

static int setup_redirects_output(int *pipefd, int **output_fds, const int n_files, const Outfile *files) {

    if (!n_files) return 0; // do nothing if no output files
    
    // 1. open all files in write-only mode and append if ">>", create with rw-r-r-- permissions if it doesnt exist

    *output_fds = malloc(n_files*sizeof(int)); 
    if (! *output_fds) {
        perror("cshell: malloc failed during output redirection");
        return 2;
    }
    
    for (int i = 0; i < n_files; i++) {  
        int append = files[i].append ? O_APPEND : O_TRUNC;            
        
        (*output_fds)[i] = open(files[i].path, O_WRONLY | O_CREAT | append, 0644);  
        
        if ((*output_fds)[i] == -1) {   // FILE NOT WRITABLE, return immediately      
            // close already open file descriptors before returning; free malloc'd pointer 

            while (i > 0) close((*output_fds)[--i]);   
            free(*output_fds); *output_fds = NULL;

            return 1;  
        }
    }

    // 2. create Command -->|--|--> Consumer pipe 
    
    if (pipe(pipefd) == -1) { 
        perror("cshell: pipe() failed for output redirection"); 
        
        for (int i = 0; i < n_files; i++) close((*output_fds)[i]);
        free(*output_fds); *output_fds = NULL;
        return 2; 
    }

    return 0;
}

static void worker_feed(int *input_fds, int n_ins, int pipe_write_fd) {
    char buf[4096]; 
    ssize_t n;
    
    // for each input file, read from it
    for (int i = 0; i < n_ins; i++) {
        //for each chunk read, write the entire chunk to pipe before reading from next input file 
        // stop immediately if write to pipe fails
        while((n = read(input_fds[i], buf, sizeof(buf))) > 0)
            if (write_all(pipe_write_fd, buf, n)) {
                    
                close(pipe_write_fd); close(input_fds[i]); 
                _exit(1);
            }
    
        close(input_fds[i]);
    }
    
    close(pipe_write_fd);
    free(input_fds);
    _exit(0);
}

static void worker_consume(int *output_fds, int n_outs, int pipe_read_fd) {
    char buf[4096];
    ssize_t n;

    // read from pipe
    while((n = read(pipe_read_fd, buf, sizeof(buf))) > 0) { 
        // for each chunk read, write to all output files
        // stop immediatelyif write to pipe fails
        for (int i = 0; i < n_outs; i++)
            if(write_all(output_fds[i], buf, n)) {
                
                close(pipe_read_fd);
                for (i = 0; i < n_outs; i++) close(output_fds[i]);
                _exit(1);
            }
    }
    close(pipe_read_fd);

    for (int i = 0; i < n_outs; i++) 
        close(output_fds[i]);
    
    _exit(0);
}

// returns a heap-allocated executable path
// caller always frees a non-NULL return
static char* resolve_path(const char *name) {
    
    bool pathenv_only = name[0] == '%'; // skips directory check
        
    // --> 1. search in directory for file path or executable 
    if (!pathenv_only) {
        
        // slash means a literal path
        if (strchr(name, '/') != NULL) 
            return (access(name, X_OK) == 0) ? strdup(name) : NULL;
        
        // must be an executable, check in cwd first
        size_t len = strlen(name) + 3;
        char *try_cwd = malloc(len);
        
        if (!try_cwd) {
            perror("cshell: malloc failure");
            return NULL;
        }
        snprintf(try_cwd, len, "./%s", name);
        
        if (access(try_cwd, X_OK) == 0) 
            return try_cwd;
    
        free(try_cwd);
    }

    // --> 2. command not found in cwd so check PATH
    if (pathenv_only) name++; 

    const char *path = getenv("PATH");
    if (!path) {
        perror("cshell: getenv() failed to access _PATH_");
        return NULL;
    }

    char *path_copy = strdup(path);  // dangerous to use getenv() return pointer directly
    if (!path_copy) {
        perror("cshell: strdup failed before _PATH_ was ever accessed");
        return NULL;
    } 

    // split PATH into directories, check for command name
    char *dir = strtok(path_copy, ":"); 
    while (dir != NULL) {

        size_t len = strlen(dir) + 2 + strlen(name);
        char *try_path = malloc(len);
        
        if (!try_path) {
            perror("cshell: malloc failed while traversing _PATH_");
            
            free(path_copy); 
            return NULL;
        }
        snprintf(try_path, len, "%s/%s", dir, name);
       
        if (access(try_path, X_OK) == 0) {   // match found !!
            free(path_copy);
            return try_path;
        }
        free(try_path);
        
        dir = strtok(NULL, ":");
    }
    free(path_copy);
    
    return NULL;
}

void execute_command_group(Command *cmd) {
    if (!cmd) return;
      
    if (cmd->connector == '|') {
        execute_pipeline(cmd);
        return;
    }
    
    pid_t child_pid;
    execute_single_command(cmd, -1, -1, &child_pid);
    
    if (child_pid > 0)
        waitpid(child_pid, NULL, 0);
}

static void execute_pipeline(Command *head) {
    Command *cur;
    int count = 0;
    
    for (cur = head; cur != NULL; cur = cur->next) {
        count++;
        if (cur->connector != '|')
            break;
    }
        
    pid_t *pids = malloc((size_t)count * sizeof(pid_t));
    if (!pids) {
        perror("cshell: malloc failed during pipeline execution");
        return;
    }
    
    // pipeline logic
}

static void execute_single_command(Command* cmd, int prev_fd, int next_fd, pid_t *child_pid) {
    int *input_fds = NULL, *output_fds = NULL;  // for input and output files of command 
    
    int stdin_fd = prev_fd;   // standard input for command; starts with read end of pipe made by pipeline
    int stdout_fd = next_fd; // input and output fd for command; starts with fd's passed by pipeline 
    
    int feeder_pipe[2] = {-1, -1}, consumer_pipe[2] = {-1, -1};          
    pid_t feeder_pid = -1, consumer_pid = -1;

    /* ------ C2: Input Redirection

        1. Open every input; if not found, handle error appropriately and return to caller 
        2. Setup Feeder -->| |--> Command pipe 
        3. Fork into Feeder, run helper function
            -> helper is responsible for closing file descriptors and freeing heap memory in case of error 
    */ 

    if (cmd->n_ins > 0) {  
        if (setup_redirects_input(feeder_pipe, &input_fds, cmd->n_ins, cmd->ins)) { 
            puts("cshell: no such file or directory");
            
            *child_pid = -1;
            return; //  command never executed 
        }

        feeder_pid = fork(); 
        if (feeder_pid == 0) { 
        // FEEDER child
            // Reads input files in order and writes the combined stream to feeder_pipe[1].
            close(feeder_pipe[0]); 
            if (prev_fd != -1) close(prev_fd); 
            if (next_fd != -1) close(next_fd);
            worker_feed(input_fds, cmd->n_ins, feeder_pipe[1]); 
        
        } if (feeder_pid < 0) {
            // fork failed
                // --> close feeder_pipe; close all open input files; free heap allocated memory
                // --> raise error and return to caller
            
            perror("cshell: fork failed");
            for (int i = 0; i < cmd->n_ins; i++) close(input_fds[i]);
            close(feeder_pipe[0]); close(feeder_pipe[1]);
            free(input_fds); input_fds = NULL;
            *child_pid = -1;
            return; 
        }

        // parent keeps the read end; command stdin now comes from feeder_pipe[0]
        close(feeder_pipe[1]);
        feeder_pipe[1] = -1;
        stdin_fd = feeder_pipe[0];
    }
    if (cmd->n_outs > 0) {
        if(setup_redirects_output(consumer_pipe, &output_fds, cmd->n_outs, cmd->outs)) { 
            puts("cshell: unable to create file for writing");
            
            if (stdin_fd != prev_fd && stdin_fd != -1) {
                close(stdin_fd);
                stdin_fd = -1;
            }
            if (feeder_pipe[0] != -1) {
                close(feeder_pipe[0]);
                feeder_pipe[0] = -1;
            }
            if (input_fds) {
                for (int i = 0; i < cmd->n_ins; i++) close(input_fds[i]);
                free(input_fds);
                input_fds = NULL;
            }
            if (feeder_pid > 0) waitpid(feeder_pid, NULL, 0);
            
            *child_pid = -1;
            return;
        }
        
        consumer_pid = fork(); 
        if (consumer_pid == 0) {
        // CONSUMER CHILD
            // Reads the command's output from consumer_pipe[0] and copies it to every output file.

            close(consumer_pipe[1]);
            if (feeder_pipe[0] != -1) close(feeder_pipe[0]);
            if (feeder_pipe[1] != -1) close(feeder_pipe[1]);
            if (stdin_fd != -1 && stdin_fd != prev_fd) close(stdin_fd);
            if(prev_fd != -1) close(prev_fd);
            
            worker_consume(output_fds, cmd->n_outs, consumer_pipe[0]);
            
        } if (consumer_pid < 0 ) { 
            // fork into Consumer child failed
                // --> close both ends of both pipes; close all open input/output files if any; free heap arrays
                // --> raise error and return to main
            
            perror("cshell: fork into consumer process failed"); 
            if (consumer_pipe[0] != -1) close(consumer_pipe[0]);
            if (consumer_pipe[1] != -1) close(consumer_pipe[1]);
            if (feeder_pipe[0] != -1) close(feeder_pipe[0]);
            
            if (cmd->n_ins > 0) for (int i = 0; i < cmd->n_ins; i++) close(input_fds[i]);
            if (cmd->n_outs > 0) for (int i = 0; i < cmd->n_outs; i++) close(output_fds[i]);
            free(input_fds); input_fds = NULL;
            free(output_fds); output_fds = NULL;

            if (stdin_fd!= -1 && stdin_fd != prev_fd) close(stdin_fd);
            if (feeder_pid > 0) waitpid(feeder_pid, NULL, 0);
            *child_pid = -1;
            return; 
        }
        
        // parent closes its copy of read-end of consumer pipe
        close(consumer_pipe[0]);
        consumer_pipe[0] = -1;
        stdout_fd = consumer_pipe[1];
    }


    /* ----------- C1: Command Execution

        1. Fork the command process
        2. In the child, connect stdin/stdout to the chosen redirection or pipeline fds
        3. Resolve the command path in the child and exec it
    */         
    
    pid_t command_pid = fork(); 
    if (command_pid == 0) {
    // COMMAND CHILD
        // --> redirect input     
        if(stdin_fd != -1  && stdin_fd != STDIN_FILENO) dup2(stdin_fd, STDIN_FILENO);
               
        // --> redirect output
        if(stdout_fd != -1 && stdout_fd != STDOUT_FILENO) dup2(stdout_fd, STDOUT_FILENO);
        
        if (stdin_fd != -1 && stdin_fd != prev_fd) close(stdin_fd);
        if (stdout_fd != -1 && stdout_fd != next_fd) close(stdout_fd);
        if (prev_fd != -1) close(prev_fd);
        if (next_fd != -1) close(next_fd);
    
        if (cmd->n_ins > 0) {
            if (feeder_pipe[0] != -1) close(feeder_pipe[0]);
            if (feeder_pipe[1] != -1) close(feeder_pipe[1]);
            for(int i = 0; i < cmd->n_ins; i++) close(input_fds[i]);
            free(input_fds); input_fds = NULL;
        }
        if (cmd->n_outs > 0) {
            if (consumer_pipe[0] != -1) close(consumer_pipe[0]);
            if (consumer_pipe[1] != -1) close(consumer_pipe[1]);
            for(int i = 0; i < cmd->n_outs; i++) close(output_fds[i]);
            free(output_fds); output_fds = NULL;
        }
        
        char *cmd_path = resolve_path(cmd->argv[0]);        
        if (!cmd_path) {
            printf("cshell: command not found (%s)\n", cmd->argv[0]);
            _exit(127);
        }

        execv(cmd_path, cmd->argv);
        
        // execv failed 
            // --> close Command child's copies of any open fds; free all heap allocated memory
            // --> raise error and exit command process
        perror("cshell: execv failed during command execution");
        free(cmd_path); cmd_path = NULL;
        _exit(127);

    } if (command_pid < 0 ) {
        perror("cshell: fork into command process failed"); 

        if (stdin_fd != -1 && stdin_fd != prev_fd) close(stdin_fd);
        if (stdout_fd != -1 && stdout_fd != next_fd) close(stdout_fd);
        if (cmd->n_ins > 0) {
            if (feeder_pipe[0] != -1) close(feeder_pipe[0]);
            if (feeder_pipe[1] != -1) close(feeder_pipe[1]);
            for (int i = 0; i < cmd->n_ins; i++) close(input_fds[i]);
            free(input_fds);
        }
        if (cmd->n_outs > 0) {
            if (consumer_pipe[0] != -1) close(consumer_pipe[0]);
            if (consumer_pipe[1] != -1) close(consumer_pipe[1]);
            for (int i = 0; i < cmd->n_outs; i++) close(output_fds[i]);
            free(output_fds);
        }
        if (feeder_pid > 0) waitpid(feeder_pid, NULL, 0);
        if (consumer_pid > 0) waitpid(consumer_pid, NULL, 0);
        
        *child_pid = -1;
        return;
    }
    
    if (stdin_fd != -1 && stdin_fd != prev_fd) close(stdin_fd);
    if (stdout_fd != -1 && stdout_fd != next_fd) close(stdout_fd);
    
    if (cmd->n_ins > 0) {
        if (feeder_pipe[0] != -1) close(feeder_pipe[0]);
        if (feeder_pipe[1] != -1) close(feeder_pipe[1]);
        for(int i = 0; i < cmd->n_ins; i++) close(input_fds[i]);
        free(input_fds); input_fds = NULL;
    }
    if (cmd->n_outs > 0) {
        if (consumer_pipe[0] != -1) close(consumer_pipe[0]);
        if (consumer_pipe[1] != -1) close(consumer_pipe[1]);
        for(int i = 0; i < cmd->n_outs; i++) close(output_fds[i]);
        free(output_fds); output_fds = NULL;
    }
    
    *child_pid = command_pid;

    if (feeder_pid > 0) waitpid(feeder_pid, NULL, 0);
    if (consumer_pid > 0) waitpid(consumer_pid, NULL, 0);
}
