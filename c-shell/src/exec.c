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
#include "redir.h"  // I/O redirection handlers and helper functions 
#include "peek.h"

static char* resolve_path(const char *name) {
    
    if (strcmp(name, "peek") == 0 || 
        strcmp(name, "locate") == 0 ||
        strcmp(name, "reveal") == 0)
        return "";

    bool pathenv_only = name[0] == '%';
    if (pathenv_only) name++;

    if (!pathenv_only) {
        if (strchr(name, '/') != NULL) {
            if (access(name, X_OK) == 0)
                return strdup(name);

            return NULL;
        }

        // commmand is an executable 
        size_t len = strlen(name) + 3;
        char *try_cwd = malloc(len);  // 2 for "./" and 1 for '\0'
        if (!try_cwd) return NULL;

        snprintf(try_cwd, len, "./%s", name);
        if (access(try_cwd, X_OK) == 0)
            return try_cwd;

        free(try_cwd);
    }

    // check in PATH 
    const char *path = getenv("PATH");
    if (!path) return NULL;

    char *path_copy = strdup(path); 
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

    free(path_copy);
    return NULL;
}

static void execute_command_simple(const SimpleCommand *cmd) {

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
        
        char *cmd_path = resolve_path(cmd.argv[0]);
        if (cmd_path == NULL) {
            unresolved = true;
            printf("cshell: command not found (%s)\n", cmd.argv[0]);
            break;
        }

        StageIO io = {
            .stdin_fd = (i == 0) ? -1 : pipes[i - 1][0],
            .stdout_fd = (i == cmd_group->count - 1) ? -1 : pipes[i][1],
            .feeder_pid = -1, 
            .consumer_pid = -1
        };

        if (prepare_stage_io(&cmd, pipes, pipe_count, &io) != 0) {
            close_stage_io(&io);
            break;
        }

        if (io.feeder_pid > 0) helper_pids[helper_count++] = io.feeder_pid;
        if (io.consumer_pid > 0) helper_pids[helper_count++] = io.consumer_pid;

        pid_t pid = fork();

        if (pid < 0) {
            perror("cshell: fork failed");
            close_stage_io(&io);
            close_all_pipes(pipes, pipe_count);
            break;
        }

        if (pid > 0 && pgid == 0) pgid = pid;

        if (pid == 0) {

            if (io.stdin_fd != -1) {
                if (dup2(io.stdin_fd, STDIN_FILENO) == -1)
                    _exit(1);
            }
            if (io.stdout_fd != -1) {
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
            execute_command_simple(&cmd);
        }

        (void)setpgid(pid, pgid);
        command_pids[command_count++] = pid;

        close_stage_io(&io);
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
