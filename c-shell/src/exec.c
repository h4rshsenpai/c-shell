#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "exec.h"
#include "hop.h"
#include "locate.h"
#include "reveal.h"
#include "redir.h"  // I/O redirection handlers and helper functions for Parts C2 and C3 
#include "peek.h"

typedef struct {
    int stdin_fd;
    int stdout_fd;
    pid_t feeder_pid;
    pid_t consumer_pid;
} StageIO;

static void close_all_pipes(int (*pipes)[2], int pipe_count);
static void close_stage_io(StageIO *io, int (*pipes)[2], int pipe_count);
static char *resolve_path(const char *name);

static void execute_pipeline(const Pipeline *pipeline);
static int execute_parent_builtin(const Pipeline *pipeline);
static int prepare_stage_io(const SimpleCommand *cmd, int pipe_in, int pipe_out, int (*pipes)[2], int pipe_count, StageIO *io);
static pid_t spawn_stage_child(const SimpleCommand *cmd, const StageIO *io, int (*pipes)[2], int pipe_count);

void execute_command(const CommandLine *cmd) {
    if (!cmd) return;

    while (waitpid(-1, NULL, WNOHANG) > 0) {}  // non-blocking cleanup for already finished background children

    for (int i = 0; i < cmd->count; i++) {
        if (execute_parent_builtin(&cmd->pipelines[i]))
            continue;
        execute_pipeline(&cmd->pipelines[i]);
    }

    while (waitpid(-1, NULL, WNOHANG) > 0) {}
}

static int execute_parent_builtin(const Pipeline *pipeline) {
    /*
        Runs builtins that must execute in the shell process itself.

        --> right now only hop needs this path
            - hop changes the shell's cwd, so forking would lose the effect
            - reuse redirection setup so builtins still work with < and >
            - restore shell stdin/stdout once builtin finishes
    */

    if (!pipeline || pipeline->count != 1 || pipeline->isBackground)
        return 0;

    const SimpleCommand *cmd = &pipeline->stages[0];
    if (!cmd->argv || cmd->argc == 0 || strcmp(cmd->argv[0], "hop") != 0)
        return 0;

    StageIO io;
    io.stdin_fd = -1;
    io.stdout_fd = -1;
    io.feeder_pid = -1;
    io.consumer_pid = -1;

    int status = prepare_stage_io(cmd, -1, -1, NULL, 0, &io);
    if (status) {
        close_stage_io(&io, NULL, 0);
        return 1;
    }

    int old_stdin = dup(STDIN_FILENO);
    int old_stdout = dup(STDOUT_FILENO);
    if (old_stdin == -1 || old_stdout == -1) {
        if (old_stdin != -1) close(old_stdin);
        if (old_stdout != -1) close(old_stdout);
        close_stage_io(&io, NULL, 0);
        return 1;
    }

    if (io.stdin_fd != -1 && io.stdin_fd != STDIN_FILENO) {
        if (dup2(io.stdin_fd, STDIN_FILENO) == -1) {
            close(old_stdin);
            close(old_stdout);
            close_stage_io(&io, NULL, 0);
            return 1;
        }
    }
    if (io.stdout_fd != -1 && io.stdout_fd != STDOUT_FILENO) {
        if (dup2(io.stdout_fd, STDOUT_FILENO) == -1) {
            dup2(old_stdin, STDIN_FILENO);
            close(old_stdin);
            close(old_stdout);
            close_stage_io(&io, NULL, 0);
            return 1;
        }
    }

    close_stage_io(&io, NULL, 0);

    // run builtin in parent so cwd changes persist for later commands
    run_hop(cmd->argc, cmd->argv);
    fflush(NULL);

    // restore shell stdio after builtin-specific redirection
    dup2(old_stdin, STDIN_FILENO);
    dup2(old_stdout, STDOUT_FILENO);
    close(old_stdin);
    close(old_stdout);

    if (io.feeder_pid > 0) waitpid(io.feeder_pid, NULL, 0);
    if (io.consumer_pid > 0) waitpid(io.consumer_pid, NULL, 0);
    return 1;
}

static void execute_pipeline(const Pipeline *pipeline) {
/*
    Runs pipeline in stages and tracks command children 
    plus any helper children created for I/O redirection.

    --> For each stage
        - start from the pipeline pipe ends inherited from neighbors
        - replace stdin with a feeder pipe when input redirection exists
        - replace stdout with a consumer pipe when output redirection exists
        - If setup fails for a stage, stop building the rest of the pipeline.
*/

    if (!pipeline || pipeline->count == 0)  
        return;

    // ------- Command I/O redirection -----------

    int pipe_count = pipeline->count - 1;
    int (*pipes)[2] = NULL;

    if (pipe_count > 0) {
        pipes = malloc((size_t)pipe_count * sizeof(*pipes));
        if (!pipes) return;

        for (int i = 0; i < pipe_count; i++) {
            pipes[i][0] = -1; pipes[i][1] = -1;
            
            if (pipe(pipes[i]) == -1) {
                perror("cshell: pipe failed while executing command");
                
                close_all_pipes(pipes, i);  // reusable helper function for cleanup
                free(pipes);
                return;
            }
        }
    }

    pid_t *command_pids = NULL;
    pid_t *helper_pids = NULL;

    command_pids = calloc((size_t)pipeline->count, sizeof(*command_pids));
    helper_pids = calloc((size_t)pipeline->count * 2, sizeof(*helper_pids));
    if (!command_pids || !helper_pids) {
        
        free(command_pids); free(helper_pids);
        close_all_pipes(pipes, pipe_count);
        free(pipes);
        return;
    }
    
    int command_count = 0;
    int helper_count = 0;

    // run pipeline in stages
    for (int i = 0; i < pipeline->count; i++) {

        SimpleCommand cmd = pipeline->stages[i];

        StageIO io;
        io.stdin_fd = -1; io.stdout_fd = -1; 
        io.feeder_pid = -1, io.consumer_pid = -1;
        
        int pipe_in = (i == 0) ? -1 : pipes[i - 1][0];
        int pipe_out = (i == pipeline->count - 1) ? -1 : pipes[i][1];

        // set stdin and stdout for child process before forking
        int setup_status = prepare_stage_io(&cmd, pipe_in, pipe_out, pipes, pipe_count, &io);
        
        if (io.feeder_pid > 0) helper_pids[helper_count++] = io.feeder_pid;
        if (io.consumer_pid > 0) helper_pids[helper_count++] = io.consumer_pid;

        if (setup_status) {
            close_stage_io(&io, pipes, pipe_count);
            break;
        }
    
    
    // --------- Command Execution -------
        pid_t pid = spawn_stage_child(&pipeline->stages[i], &io, pipes, pipe_count);
        
        if (pid > 0) {
            command_pids[command_count++] = pid;    // track child process for waitpid later
        } else if (pid < 0) {
            close_stage_io(&io, pipes, pipe_count);
            break;
        }

        // close setup fds for stage before moving to next 
        close_stage_io(&io, pipes, pipe_count); 
    }

    close_all_pipes(pipes, pipe_count);

    // wait for all stages to finish if foreground process
    if (pipeline->isBackground == false) {
        for (int i = 0; i < command_count; i++) {
            waitpid(command_pids[i], NULL, 0);
        }
        for (int i = 0; i < helper_count; i++) {
            waitpid(helper_pids[i], NULL, 0);
        }
    }

    free(command_pids);
    free(helper_pids);
    free(pipes);
}

static int prepare_stage_io(const SimpleCommand *cmd, int pipe_in, int pipe_out, int (*pipes)[2], int pipe_count, StageIO *io) {
    /* 
        setup effective stdin/stdout for this stage
        --> start with pipes passed by caller, redirect to input and output files (if any)
            --> if input redirection exists, open all input files and spawn a feeder helper to write combined contents into a pipe
            --> if output redirection exists, open all output files and spawn a consumer helper to copy command output into all targets
        --> return final fds through shared StageIO structure
    */

    io->stdin_fd = pipe_in;
    io->stdout_fd = pipe_out;
    io->feeder_pid = -1;
    io->consumer_pid = -1;

    if (cmd->n_ins > 0) {
        int *input_fds = malloc((size_t)cmd->n_ins * sizeof(*input_fds));
        if (!input_fds) return 2;

        for (int i = 0; i < cmd->n_ins; i++) {
            input_fds[i] = open(cmd->ins[i], O_RDONLY);
            
            if (input_fds[i] == -1) {
                puts("cshell: no such file or directory");    
                
                for (int j = 0; j < i; j++)
                    close(input_fds[j]);

                free(input_fds);
                return 1;
            }
        }
        
        int feeder_pipe[2];
        if (pipe(feeder_pipe) == -1) {
            perror("cshell: feeder pipe failed");
            for (int i = 0; i < cmd->n_ins; i++) 
                close(input_fds[i]);
            free(input_fds);
            return 2;
        }

        // fork into feeder child here 
            // call worker_feed to open all input files, read from all and write to command

        io->feeder_pid = fork();
        if (io->feeder_pid == 0) {               
            
            close(feeder_pipe[0]);
            close_all_pipes(pipes, pipe_count);
            if (pipe_in != -1) close(pipe_in);
            if (pipe_out != -1) close(pipe_out);
        
            worker_feed(input_fds, cmd->n_ins, feeder_pipe[1]);
        
        } if (io->feeder_pid < 0) {
            perror("cshell: fork into feeder failed");
            close(feeder_pipe[0]); close(feeder_pipe[1]);
            for (int i = 0; i < cmd->n_ins; i++) 
                close(input_fds[i]);
            free(input_fds);
            return 2;
        }

        // Shell process cleans up after feeder here
            // close only i
        for (int i = 0; i < cmd->n_ins; i++) 
            close(input_fds[i]);
        close(feeder_pipe[1]);
        io->stdin_fd = feeder_pipe[0];
        free(input_fds);
    }

    if (cmd->n_outs > 0) {
        int *output_fds = malloc((size_t)cmd->n_outs * sizeof(*output_fds));
        int consumer_pipe[2];

        if (!output_fds) {
            // cleaup i/o already setup for feeder 
            if (io->stdin_fd == pipe_in) io->stdin_fd = -1;
            if (io->stdout_fd == pipe_out) io->stdout_fd = -1;
            close_stage_io(io, pipes, pipe_count);
            return 2;
        }

        for (int i = 0; i < cmd->n_outs; i++) {
            int append_mode = cmd->outs[i].append ? O_APPEND : O_TRUNC;
            
            output_fds[i] = open(cmd->outs[i].path, O_WRONLY | O_CREAT | append_mode, 0644);
            if (output_fds[i] == -1) {
                puts("cshell: unable to create file for writing");
            
                // handle cleanup 
                for (int j = 0; j < i; j++) close(output_fds[j]);
                free(output_fds);
                if (io->stdin_fd == pipe_in) io->stdin_fd = -1;
                if (io->stdout_fd == pipe_out) io->stdout_fd = -1;
                close_stage_io(io, pipes, pipe_count);
                return 1;
            }
        }

        if (pipe(consumer_pipe) == -1) {
            perror("cshell: consumer pipe failed");
            for (int i = 0; i < cmd->n_outs; i++) close(output_fds[i]);
            
            if (io->stdin_fd == pipe_in) io->stdin_fd = -1;
            if (io->stdout_fd == pipe_out) io->stdout_fd = -1;
            close_stage_io(io, pipes, pipe_count);
            
            free(output_fds);
            return 2;
        }

        io->consumer_pid = fork();
        if (io->consumer_pid == 0) {    // CONSUMER child

            close(consumer_pipe[1]);
            close_all_pipes(pipes, pipe_count);
            if (pipe_in != -1) close(pipe_in);
            if (pipe_out != -1) close(pipe_out);
            if (io->stdin_fd != -1) close(io->stdin_fd);

            worker_consume(output_fds, cmd->n_outs, consumer_pipe[0]);
        
        } if (io->consumer_pid < 0) {
            perror("cshell: fork into consumer failed");
            close(consumer_pipe[0]);
            close(consumer_pipe[1]);
            for (int i = 0; i < cmd->n_outs; i++) 
                close(output_fds[i]);
            if (io->stdin_fd == pipe_in) io->stdin_fd = -1;
            if (io->stdout_fd == pipe_out) io->stdout_fd = -1;
            
            close_stage_io(io, pipes, pipe_count);
    
            free(output_fds);
            return 2;
        }

        for (int i = 0; i < cmd->n_outs; i++) 
            close(output_fds[i]);
        close(consumer_pipe[0]);
        io->stdout_fd = consumer_pipe[1];
        free(output_fds);
    }
    return 0;
}

static pid_t spawn_stage_child(const SimpleCommand *cmd, const StageIO *io, int (*pipes)[2], int pipe_count) {
    int status;
    
    pid_t pid = fork();
    if (pid < 0) {
        perror("cshell: fork failed");
        return -1;
    }

    if (pid == 0) {

        if (io->stdin_fd != -1 && io->stdin_fd != STDIN_FILENO) {
            status = dup2(io->stdin_fd, STDIN_FILENO); 
            if (status == -1) _exit(1);

            close(io->stdin_fd);
        }
        if (io->stdout_fd != -1 && io->stdout_fd != STDOUT_FILENO) {
            status = dup2(io->stdout_fd, STDOUT_FILENO); 
            if (status == -1) _exit(1);
            
            close(io->stdout_fd);
        }

        close_all_pipes(pipes, pipe_count);

        // PART B: BUILTINS
        // execpt hop, all other builtins are run like any other child command with piped input and output
        if (strcmp(cmd->argv[0], "peek") == 0) {
            run_peek(cmd->argc, cmd->argv);
            fflush(NULL);
            _exit(0);
        } 
        else if (strcmp(cmd->argv[0], "locate") == 0) {
            run_locate(cmd->argc, cmd->argv);
            fflush(NULL);
            _exit(0);
        } 
        else if (strcmp(cmd->argv[0], "reveal") == 0) {
            run_reveal(cmd->argc, cmd->argv);
            fflush(NULL);
            _exit(0);
        } 
        else {
        // not abuiltin
        // use resolve_path + exec for arbitrary command
            
            char *cmd_path = resolve_path(cmd->argv[0]);
            if (!cmd_path) {
                printf("cshell: command not found (%s)\n", cmd->argv[0]);
                free(cmd_path);
                _exit(127);
            }
            
            execv(cmd_path, cmd->argv);
            // execv never reaches here unless it fails
                // child exits after raising error
            perror("cshell: exec failed");
            free(cmd_path);
            _exit(126);
        }
    }
    return pid;
}

static char *resolve_path(const char *name) {
    bool pathenv_only = name[0] == '%';

    if (pathenv_only) name++;

    if (!pathenv_only) {
        if (strchr(name, '/') != NULL) {
            // command is a literal path -> search in cwd
                // if executable and exists, return path else return NULL 
            if (access(name, X_OK) == 0) 
                return strdup(name); 
    
            return NULL;
        }

        // commmand is an executable -> search in cwd
            // if exists and user has exec permissons, return path, else skip to PATH check
        size_t len = strlen(name) + 3;
        char *try_cwd = malloc(len);  // 2 for "./" and 1 for '\0'
        if (!try_cwd) return NULL;

        snprintf(try_cwd, len, "./%s", name);
        if (access(try_cwd, X_OK) == 0) 
            return try_cwd;

        free(try_cwd);
    }

    // C1.3 - PATH check
    
    const char *path = getenv("PATH");
    if (!path) return NULL;

    char *path_copy = strdup(path); // don't use pointer returned by getenv !!
    if (!path_copy) return NULL;

    char *ptr = NULL;
    char *dir = strtok_r(path_copy, ":", &ptr); // strtok() is not thread safe

    while (dir) {
        size_t len = strlen(dir) + strlen(name) + 2;
        char *try_path = malloc(len); // 1 for '/' and 1 for '\0'
        if (!try_path) { free(path_copy); return NULL; }

        snprintf(try_path, len, "%s/%s", dir, name);
        
        if (access(try_path, X_OK) == 0) {  // FOUND !!
            free(path_copy);
            return try_path;
        }
    
        free(try_path);
        dir = strtok_r(NULL, ":", &ptr);
    }

    // Commmand not found so return NULL --> caller prints error message
    free(path_copy);
    return NULL;
}

static void close_stage_io(StageIO *io, int (*pipes)[2], int pipe_count) {
    
    if (io->stdin_fd != -1) {
        close(io->stdin_fd);
        for (int i = 0; i < pipe_count; i++) {
            if (pipes[i][0] == io->stdin_fd)
                pipes[i][0] = -1;
            if (pipes[i][1] == io->stdin_fd)
                pipes[i][1] = -1;
        }

        io->stdin_fd = -1;
    } 
    if (io->stdout_fd != -1) {
        close(io->stdout_fd);
        for (int i = 0; i < pipe_count; i++) {
            if (pipes[i][0] == io->stdout_fd)
                pipes[i][0] = -1;
            if (pipes[i][1] == io->stdout_fd)
                pipes[i][1] = -1;
        }
        io->stdout_fd = -1;
    } 
    return;
}

static void close_all_pipes(int (*pipes)[2], int pipe_count) {
    for (int i = 0; i < pipe_count; i++) {
        if (pipes[i][0] != -1) {
            close(pipes[i][0]);
            pipes[i][0] = -1;
        }
        if (pipes[i][1] != -1) {      
            close(pipes[i][1]);
            pipes[i][1] = -1;
        }
    }
}
