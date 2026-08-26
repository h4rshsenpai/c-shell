#include <dirent.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "hop.h"
#include "prompt.h"
#include "reveal.h"

static int lambda(const void *a, const void *b);
static int list_dir(const char *path, const char *prefix, bool a_flag, bool t_flag);
static int resolve_target(const char *name, char *target, size_t size);

void run_reveal(int argc, char **argv) {
    bool a_flag = false;
    bool t_flag = false;
    char *name = NULL;

    for (int i = 1; i < argc; i++) {
        if (argv[i][0] != '-') {     // not a flag -> if multiple directories, return with syntax error 
            if (name) {
                puts("reveal: invalid syntax");
                return;
            }
            name = argv[i];
            continue;
        }

        char *flag = argv[i] + 1;
        if (*flag == '\0') {    // flag cannot be -, "reveal dir_name -" should be invalid
            if (name) {
                puts("reveal: invalid syntax");
                return;
            }
            name = argv[i];
            continue;
        }

        while (*flag != '\0') {
            if (*flag == 'a') a_flag = true;
            else if (*flag == 't') t_flag = true;
            else {
                puts("reveal: invalid syntax");
                return;
            }
            flag++;
        }
    } // --> argument parsing done, syntax is valid !!
    
  
    char target[PATH_MAX];
    if (!name) {
        // reveal current working dir
        if(!getcwd(target, sizeof(target))) {
            perror("reveal: getcwd failed");
            return;
        }
    } else {
        int status = resolve_target(name, target, sizeof(target));
        if (status) {
            puts("reveal: no such directory");
            return;
        }
    }
  
    // printf("parsed: %s, a: %d, t: %d\n", name, a_flag, t_flag);
    // printf("resolved path: %s\n", target);
    
    struct stat info;
    // printf("Calling stat on %s\n", target);
    if (stat(target, &info) != 0 || !S_ISDIR(info.st_mode)) {
        perror("stat failed");
        puts("reveal: no such directory");
        return;
    }

    // printf("calling listddir on %s\n",target);
    if (list_dir(target, "", a_flag, t_flag) != 0)
        puts("reveal: no such directory");
}

static int resolve_target(const char *name, char *target, size_t size) {
    if (name[0] == '/') {
        char *temp = strdup(name);
        if (!temp) return 1;
        target = temp;
        return 0;
    }

    if (strcmp(name, "~") == 0) {
        const char *home = shell_get_home();
        if (!home) return 1;
        snprintf(target, size, "%s", home);
        return 0;
    }

    if (strcmp(name, ".") == 0) {
        if (!getcwd(target, size)) return 1;
        return 0;
    }

    if (strcmp(name, "..") == 0) {
        if (!getcwd(target, size)) return 1;
        size_t len = strlen(target);

        if (target[len-1] == '/') len--;    // remove trailing / if any
        while (len > 1 && target[len - 1] != '/') len--;
        if (len == 0) len = 1;

        target[len] = '\0';
        return 0;
    }

    if (strcmp(name, "-") == 0) {
        const char *prev = hop_prev_dir();
        if (!prev) return 1;
        char *temp = strdup(name);
        if (!temp) return 1;
        target = temp;
        return 0;
    }

    char cwd[PATH_MAX];
    if (!getcwd(cwd, sizeof(cwd))) return 1;
    snprintf(target, size, "%s/%s", cwd, name);
    return 0;
}

// opens directory using opendir() 
// reads directory items in a loop using readdir()
static int list_dir(const char *path, const char *prefix, bool a_flag, bool t_flag) {
    DIR *dir = opendir(path);
    if (!dir) return 1;

    char **names = NULL;
    int count = 0, cap = 0;
    
    struct dirent *entry = readdir(dir);
    for (; entry != NULL; entry = readdir(dir)) {

        // printf("traversing dirs, entry is %s\n", entry->d_name);
        // skip hidden file if a_flag is not set
        if (entry->d_name[0] == '.') {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)    
                continue;
            if(!a_flag) continue; 
        }

        if (count == cap) {
            int new_cap = cap ? cap * 2 : 16;
            char **temp = realloc(names, (size_t)new_cap * sizeof(*temp));
            if (!temp) {
                perror("reveal: realloc failed");

                closedir(dir);
                for (int i = 0; i < count; i++) free(names[i]);
                free(names);
                return 1;
            }
            names = temp;
            cap = new_cap;
        }

        names[count] = strdup(entry->d_name);
        if (!names[count]) {    // raise error and cleanup heap allocated memory
            perror("reveal: strdup failed");
    
            closedir(dir);
            for (int i = 0; i < count; i++) free(names[i]);
            free(names);
            return 1;
        }
        count++;
    }
    closedir(dir);
    
    qsort(names, (size_t)count, sizeof(char*), lambda);
    
    for (int i = 0; i < count; i++) {
        char full_path[PATH_MAX];
        snprintf(full_path, sizeof(full_path), "%s/%s", path, names[i]);
        
        struct stat info;
        if (stat(full_path, &info) != 0) {
            for (int j = 0; j < count; j++) free(names[j]);
            free(names);
            return 1;
        }

        if(t_flag && S_ISDIR(info.st_mode)) 
            printf("%s%s/\n", prefix, names[i]);
        else
            printf("%s%s\n", prefix, names[i]);

        // if -t flag is recursive, recursively list directories under target
        // skip . and .. to prevent infinite recursive loop
        if (strcmp(names[i], ".") == 0 || strcmp(names[i], "..") == 0)
            continue;
        
        if (t_flag && S_ISDIR(info.st_mode)) {
            char next_prefix[PATH_MAX];
            snprintf(next_prefix, sizeof(next_prefix), "%s%s/", prefix, names[i]);

            int status = list_dir(full_path, next_prefix, a_flag, t_flag);
            if (status) {
                for (int j = 0; j < count; j++) free(names[j]);
                free(names);
                return 1;
            }
        }
    }

    for (int i = 0; i < count; i++) free(names[i]);
    free(names);
    return 0;
}

static int lambda(const void *a, const void *b) {
    const char *left = *(const char **)a;
    const char *right = *(const char **)b;
    return strcmp(left, right);
}
