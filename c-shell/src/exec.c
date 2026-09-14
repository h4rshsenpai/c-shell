#include <fcntl.h>
#include <stdbool.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "exec.h"
#include "hop.h"
#include "jobs.h"
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

static void close_stage_io(StageIO *io, int (*pipes)[2], int pipe_count);
static void close_all_pipes(int (*pipes)[2], int pipe_count);

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

static bool command_is_resolvable(const SimpleCommand *cmd) {
    if (strcmp(cmd->argv[0], "peek") == 0 ||
        strcmp(cmd->argv[0], "locate") == 0 ||
        strcmp(cmd->argv[0], "reveal") == 0)
        return true;

    char *cmd_path = resolve_path(cmd->argv[0]);
    if (!cmd_path)
        return false;

    free(cmd_path);
    return true;
}

static void execute_command(const SimpleCommand *cmd) {
    // Run Built

    if (strcmp(cmd->argv[0], "peek") == 0) {
        run_peek(cmd->argc, cmd->argv);
        fflush(NULL);
        _exit(0);
    }
    if (strcmp(cmd->argv[0], "locate") == 0) {
        run_locate(cmd->argc, cmd->argv);
        fflush(NULL);
        _exit(0);
    }
    if (strcmp(cmd->argv[0], "reveal") == 0) {
        run_reveal(cmd->argc, cmd->argv);
        fflush(NULL);
        _exit(0);
    }

    char *cmd_path = resolve_path(cmd->argv[0]);
    if (!cmd_path) {
        printf("cshell: command not found (%s)\n", cmd->argv[0]);
        _exit(127);
    }

    execv(cmd_path, cmd->argv);
    perror("cshell: exec failed");
    free(cmd_path);
    _exit(126);
}

static int execute_command_group(const CommandGroup *cmd_group) {
/*
    Runs cmd_group in stages and tracks command children
    plus any helper children created for I/O redirection.

    --> For each stage
        - start from the cmd_group pipe ends inherited from neighbors
        - replace stdin with a feeder pipe when input redirection exists
        - replace stdout with a consumer pipe when output redirection exists
        - If setup fails for a stage, stop building the rest of the cmd_group.
*/
    pid_t pgid = 0;
    int pipe_count = cmd_group->count - 1;
    int (*pipes)[2] = NULL;

    if (pipe_count > 0) {

        pipes = malloc((size_t)pipe_count * sizeof(*pipes));
        if (!pipes) return 0;

        for (int i = 0; i < pipe_count; i++) {

            pipes[i][0] = -1;
            pipes[i][1] = -1;

            if (pipe(pipes[i]) == -1) {
                perror("cshell: pipe failed while executing command");

                close_all_pipes(pipes, i);
                free(pipes);
                return 0;
            }
        }
    }

    pid_t *command_pids = NULL;
    pid_t *helper_pids = NULL;
    command_pids = calloc((size_t)cmd_group->count, sizeof(*command_pids));
    helper_pids = calloc((size_t)cmd_group->count * 2, sizeof(*helper_pids));

    if (!command_pids || !helper_pids) {

        free(command_pids); free(helper_pids);
        close_all_pipes(pipes, pipe_count);
        free(pipes);
        return 0;
    }

    int command_count = 0;
    int helper_count = 0;
    bool unresolved = false;

    for (int i = 0; i < cmd_group->count; i++) {

        SimpleCommand cmd = cmd_group->list[i];

        if (command_is_resolvable(&cmd) != true) {
            unresolved = true;
            printf("cshell: command not found (%s)\n", cmd.argv[0]);
            break;
        }

        // shell sets stdin and stdout for child process before forking

        int pipe_in = (i == 0) ? -1 : pipes[i - 1][0];
        int pipe_out = (i == cmd_group->count - 1) ? -1 : pipes[i][1];
        StageIO io = {.stdin_fd = -1, .stdout_fd = -1, .feeder_pid = -1, .consumer_pid = -1};

        int setup_status = prepare_stage_io(&cmd, pipe_in, pipe_out, pipes, pipe_count, &io);
        if (setup_status) {
            close_stage_io(&io, pipes, pipe_count);
            break;
        }
        if (io.feeder_pid > 0) helper_pids[helper_count++] = io.feeder_pid;
        if (io.consumer_pid > 0) helper_pids[helper_count++] = io.consumer_pid;


        pid_t pid = fork();
        if (pid < 0) {
            perror("cshell: fork failed");
            close_stage_io(&io, pipes, pipe_count);
            break;
        }

        if (pid > 0 && pgid == 0) pgid = pid;

        if (pid == 0) {
            if (io.stdin_fd != -1 && io.stdin_fd != STDIN_FILENO) {
                if (dup2(io.stdin_fd, STDIN_FILENO) == -1)
                    _exit(1);
            }
            if (io.stdout_fd != -1 && io.stdout_fd != STDOUT_FILENO) {
                if (dup2(io.stdout_fd, STDOUT_FILENO) == -1)
                    _exit(1);
            }

            // background process with no input files should not have access to terminal input
            if (cmd_group->isBackground && io.stdin_fd == -1) { 
                int null_input = open("/dev/null", O_RDONLY);
                if (null_input == -1 || dup2(null_input, STDIN_FILENO) == -1)
                    _exit(1);
                close(null_input);
            }

            if (pgid == 0) pgid = getpid(); // first child sets the group pid
                                            // every other child joins the same group
            (void)setpgid(0, pgid);

            close_all_pipes(pipes, pipe_count);
            execute_command(&cmd);
        }

        (void)setpgid(pid, pgid);
        command_pids[command_count++] = pid;
        close_stage_io(&io, pipes, pipe_count);
    }

    close_all_pipes(pipes, pipe_count);

    if (cmd_group->isBackground && command_count > 0) {

        int job = add_job(pgid, cmd_group->list[0].argv[0], true);
        if (job < 0) return 1;

        for (int i = 0; i < command_count; i++) {
            if(add_process(job, command_pids[i], cmd_group->list[i].argv[0]) != 0)
               return 1;
        }
        printf("[%d] %ld\n", job_number(job), (long)pgid);
        fflush(stdout);
    }
    else {  // background process completion should not interrupt foregground
            // if waitpid on fg is interruped because of SIGCHILD, keep waiting
        for (int i = 0; i < command_count; i++)
            while (waitpid(command_pids[i], NULL, 0) == -1 && errno == EINTR);

        for (int i = 0; i < helper_count; i++)
            while (waitpid(helper_pids[i], NULL, 0) == -1 && errno == EINTR);
    }

    free(command_pids);
    free(helper_pids);
    free(pipes);

    return unresolved;
}

void execute_command_line(const CommandLine *cmd_line) {
    if (!cmd_line) return;

    jobs_reap_and_report();

    for (int i = 0; i < cmd_line->count; i++) {

        CommandGroup *cmd_group = &cmd_line->list[i];

        // hop changes shell's working directory so must run in shell
        if (cmd_group->count == 1 && !cmd_group->isBackground && strcmp(cmd_group->list[0].argv[0], "hop") == 0) {
            run_hop(cmd_group->list[0].argc, cmd_group->list[0].argv);
            continue;
        }

        // activities is trivial so doesn't require a forked process overhead
        if (cmd_group->count == 1 && !cmd_group->isBackground && strcmp(cmd_group->list[0].argv[0], "activities") == 0) {
            jobs_reap_and_report();
            // jobs_print();
            continue;
        }

        if (execute_command_group(cmd_group)) break;
    }
}
