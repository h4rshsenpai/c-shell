#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "parser.h"     // Command struct
#include "builtin.h"

void run_cmd(Command *cmd) {
    if (!cmd || !cmd->argv || !cmd->argv[0]) {
        return;
    }

    if (strcmp(cmd->argv[0], "hop") == 0) return builtin_hop(cmd);
    if (strcmp(cmd->argv[0], "peek") == 0) return builtin_peek(cmd);
    if (strcmp(cmd->argv[0], "locate") == 0) return builtin_locate(cmd);
    if (strcmp(cmd->argv[0], "reveal") == 0) return builtin_reveal(cmd);

    // not a builtin, external command 
    char *cmd_path = resolve_path(cmd->argv[0]);
    if (!cmd_path) {
        printf("cshell: command not found (%s)\n", cmd->argv[0]);
        return;
    }

    pid_t pid = fork();
    if (pid < 0) {
        perror("cshell: fork failed");
        free(cmd_path);
        return;
    }

    if (pid == 0) {
        if (setup_redirects(cmd) != 0) {
            _exit(1);
        }
        execv(cmd_path, cmd->argv);
        perror("execv failed");
        _exit(1);
    }

    if (!cmd->isBackground) {
        waitpid(pid, NULL, 0);
    }

    free(cmd_path);
}

char* resolve_path(char *name) {
    
    if (strchr(name, '/')) {
        if (access(name, X_OK) == 0) {
            return strdup(name);
        }
        return NULL;
    }

    if (access(name, X_OK) == 0) {
        return strdup(name);
    }

    char *path_env = getenv("PATH");
    if (!path_env || !*path_env) {
        return NULL;
    }

    char *path_copy = strdup(path_env);
    if (!path_copy) {
        return NULL;
    }

    char *token = strtok(path_copy, ":");
    while (token != NULL) {
        size_t len = strlen(token) + strlen(name) + 2;
        char *candidate = malloc(len);
        if (!candidate) {
            free(path_copy);
            return NULL;
        }

        snprintf(candidate, len, "%s/%s", token, name);
        if (access(candidate, X_OK) == 0) {
            free(path_copy);
            return candidate;
        }

        free(candidate);
        token = strtok(NULL, ":");
    }

    free(path_copy);
    return NULL;
}

static int setup_redirects(Command *cmd) {
    for (int i = 0; i < cmd->n_ins; i++) {
        int fd = open(cmd->ins[i], O_RDONLY);
        if (fd < 0) {
            perror(cmd->ins[i]);
            return -1;
        }
        if (dup2(fd, STDIN_FILENO) < 0) {
            perror("dup2");
            close(fd);
            return -1;
        }
        close(fd);
    }

    for (int i = 0; i < cmd->n_outs; i++) {
        int flags = O_WRONLY | O_CREAT | (cmd->outs[i].append ? O_APPEND : O_TRUNC);
        int fd = open(cmd->outs[i].path, flags, 0644);
        if (fd < 0) {
            perror(cmd->outs[i].path);
            return -1;
        }
        if (dup2(fd, STDOUT_FILENO) < 0) {
            perror("dup2");
            close(fd);
            return -1;
        }
        close(fd);
    }

    return 0;
}

c
