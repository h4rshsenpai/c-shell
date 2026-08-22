#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "parser.h"     // Command struct
#include "exec.h"
// #include "builtin.h"

static char* resolve_path(const char* cmd_name); 

void execute_command_group(Command *cmd) {
/*
    if (strcmp(cmd->argv[0], "hop") == 0) return builtin_hop(cmd);
    if (strcmp(cmd->argv[0], "peek") == 0) return builtin_peek(cmd);
    if (strcmp(cmd->argv[0], "locate") == 0) return builtin_locate(cmd);
    if (strcmp(cmd->argv[0], "reveal") == 0) return builtin_reveal(cmd);
*/
    // not a builtin, external command -> resolve command name 
    char *cmd_path = resolve_path(cmd->argv[0]);
    if (!cmd_path) {
        printf("cshell: command not found (%s)\n", cmd_path);
        return;
    }

    // execute 
    pid_t pid = fork();
    if (pid < 0) {
        perror("cshell: fork failed");
        free(cmd_path);
        return;
    }

    if (pid == 0) {
        // if (setup_redirects(cmd) != 0) {
            // _exit(1);
        // }
        execv(cmd_path, cmd->argv);
        perror("execv failed");
        _exit(1);
    }

    if (!cmd->isBackground) {
        waitpid(pid, NULL, 0);
    }

    free(cmd_path);
}

// returns a heap-allocated executable path
// Caller always frees a non-NULL return
static char* resolve_path(const char *cmd_name) {
    const char *name = cmd_name;
    bool path_only = name[0] == '%';
    
    if (path_only) name++;
    
    // A slash means a literal path
    if (!path_only && strchr(name, '/') != NULL) 
        return (access(name, X_OK) == 0) ? strdup(name) : NULL;
    
    // cmd_name is an executable
    // check in cwd first
    if (!path_only) {
        size_t len = strlen(name) + 3;
        char *try_cwd = malloc(len);
        
        if (!try_cwd) {
            perror("cshell: malloc failure");
            return NULL;
        }
        snprintf(try_cwd, len, "./%s", name);
        
        if (access(try_cwd, X_OK) == 0) return try_cwd;
        free(try_cwd);
    }

    // check PATH
    // if path_only is true, function starts here directly
    char *path = getenv("PATH");
    if (!path) {
        perror("cshell: malloc failure");
        return NULL;
    }

    char *path_copy = strdup(path);  // dangerous to use getenv() return pointer directly
    if (!path_copy) {
        perror("cshell: malloc failure");
        return NULL;
    }

    // traverse listed directories
    char *dir = strtok(path_copy, ":");
    while (dir != NULL) {
        size_t len = strlen(dir) + 2 + strlen(name);
        char *try_path = malloc(len);
        
        if (!try_path) {
            free(path_copy);
            perror("cshell: malloc failure");
            return NULL;
        }
        
        snprintf(try_path, len, "%s/%s", dir, name);
       
        if (access(try_path, X_OK) == 0) {
            free(path_copy);
            return try_path;
        }
        free(try_path);
        
        dir = strtok(NULL, ":");
    }
    free(path_copy);
    
    return NULL;
}

