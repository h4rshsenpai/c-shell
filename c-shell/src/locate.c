#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "locate.h"

static int add_path(char ***paths, int *n_paths, int *cap, const char *path);
static int find_matches(const char *name, char ***paths, int *n_paths, int *cap);
static int abs_path(const char *path, char *out_path, size_t size);

void run_locate(int argc, char **argv) {
    if (argc <= 1) {
        puts("locate: invalid syntax");
        return;
    }

    char ***paths = calloc((size_t)(argc - 1), sizeof(*paths));
    int *match_count = calloc((size_t)(argc - 1), sizeof(*match_count));
    int *caps = calloc((size_t)(argc - 1), sizeof(*caps));
    
    if (!paths || !match_count || !caps) {
        free(paths); free(match_count); free(caps);
        return;
    }

    for (int i = 1; i < argc; i++)
        if (find_matches(argv[i], &paths[i - 1], &match_count[i - 1], &caps[i - 1]) != 0) {
            // no match found for this command
            // cleanup
            for (int j = 0; j < i - 1; j++) {
                for (int k = 0; k < match_count[j]; k++)
                    free(paths[j][k]);
                free(paths[j]);
            }
            free(paths);
            free(match_count);
            free(caps);
            return;
        }
 
    // matches were found, print matches first, then error messages for commands 
    // with no match at end
    for (int i = 0; i < argc-1; i++)
        for (int j = 0; j < match_count[i]; j++)
            puts(paths[i][j]);
    
    for (int i = 0; i < argc - 1; i++)
        if (match_count[i] == 0)
            printf("locate: command not found (%s)\n", argv[i+1]);
    
    for (int i = 0; i < argc - 1; i++) {
        for (int j = 0; j < match_count[i]; j++)
            free(paths[i][j]);
        free(paths[i]);
    }

    free(paths);
    free(match_count);
    free(caps);
}

static int find_matches(const char *name, char ***paths, int *n_paths, int *cap) {
    
// first check current working directory

    char *cmd_name; 
    int cmd_len = strlen(name) + 3;
    cmd_name = malloc(cmd_len * sizeof(char));    
    if (!cmd_name) return 1;

    snprintf(cmd_name, cmd_len, "./%s", name);
    
    char abs[PATH_MAX];
    if (access(cmd_name, X_OK) == 0) {

        if (abs_path(cmd_name, abs, sizeof(abs)) != 0) {
            free(cmd_name);
            return 1;
        }
        if (add_path(paths, n_paths, cap, abs) != 0) {
            free(cmd_name);
            return 1;
        }
    }
    free(cmd_name);
// now search PATH

    char *path_env = getenv("PATH");
    if (!path_env) return 0;

    char *path_copy = strdup(path_env);
    if (!path_copy) return 1;

    char *temp = NULL;
    char *dir = strtok_r(path_copy, ":", &temp);
    while (dir) {
    
        ssize_t len = strlen(dir) + strlen(name) + 2;
        char *try_path = malloc(len);
        if (!try_path) {
            free(path_copy);
            return 1;
        }

        snprintf(try_path, len, "%s/%s", dir, name);
        if (access(try_path, X_OK) == 0) {
            // match found
            if (abs_path(try_path, abs, sizeof(abs)) != 0) {
                free(try_path); free(path_copy);
                return 1;
            }
            if (add_path(paths, n_paths, cap, abs) != 0) {
                free(try_path); free(path_copy);
                return 1;
            }
        }
        free(try_path);
        dir = strtok_r(NULL, ":", &temp);
    }

    free(path_copy);
    return 0;
}


static int abs_path(const char *path, char *out_path, size_t size) {
    if (path[0] == '/') {
        // already absolute path
        snprintf(out_path, size, "%s", path);
        return 0;
    }

    char cwd[PATH_MAX];
    if (!getcwd(cwd, sizeof(cwd))) return 1;

    snprintf(out_path, size, "%s/%s", cwd, path);
    return 0;
}

static int add_path(char ***paths, int *n_paths, int *cap, const char *path) {
    
    if (*n_paths == *cap) {
        int new_cap = *cap ? *cap * 2 : 8;
        char **temp = realloc(*paths, (size_t)new_cap * sizeof(*temp));
        if (!temp) return 1;

        *paths = temp;
        *cap = new_cap;
    }

    (*paths)[*n_paths] = strdup(path);
    if (!(*paths)[*n_paths]) return 1;

    (*n_paths)++;
    return 0;
}
