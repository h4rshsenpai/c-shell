
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <limits.h>
#include <stdbool.h>

#include "hop.h"
#include "prompt.h"

static char PREV_DIR[PATH_MAX];
static bool HAS_PREV = false;

typedef struct {
    char path[PATH_MAX];
    long long visits;
    long long last_visit;
} HopEntry;


// variables for loading, updating, and pruning history

static HopEntry *HOP_HISTORY = NULL;
static size_t HOP_HISTORY_COUNT = 0;
static size_t HOP_HISTORY_CAP = 0;
static char HOP_HISTORY_FILE[PATH_MAX];
enum { HOP_VISIT_WEIGHT_SECONDS = 90 * 24 * 60 * 60 };

// helper functions

static int grow_history(size_t needed);  // helper for dynamic growth of HOP_HISTORY_FILE
static int save_history_file(void);
static void prune_history(void);

static int frecency_match(const char *name, char *target, size_t size);
static long long current_history_score(const HopEntry *entry);
static void resolve_target(const char *name, char *target, size_t size);
static int try_hop(const char *path);
static int record_hop(const char *path);

const char *hop_prev_dir(void) {
    if (!HAS_PREV) return NULL;
    return PREV_DIR;
}

void hop_init(void) {
    const char *shell_home = shell_get_home();
    // if no home is initilaized for some reason, hop will fallback to non-persistent frecency using stack memory
    if (!shell_home) {
        HOP_HISTORY_FILE[0] = '\0';
        return;
    }

    snprintf(HOP_HISTORY_FILE, sizeof(HOP_HISTORY_FILE), "%s/.hop_history", shell_home);
    
    // Load the history file everytime shell starts 
    // read entries and store it on stack memory for later hops
    
    FILE *fp = fopen(HOP_HISTORY_FILE, "r");
    if (!fp) {
        if (access(HOP_HISTORY_FILE, F_OK) == 0)
            perror("c-shell: error loading history for hop");
        return;
    }

    char line[PATH_MAX + 128];
    while (fgets(line, sizeof(line), fp)) {
        long long visits = 0, last_visit = 0;
        char path[PATH_MAX];

        sscanf(line, "%lld\t%lld\t%4095[^\n]", &visits, &last_visit, path);
        if (visits <= 0 || path[0] == '\0')
            continue;

        ssize_t index = -1;
        for (size_t i = 0; i < HOP_HISTORY_COUNT; i++)
            if (strcmp(HOP_HISTORY[i].path, path) == 0) {
                index = (ssize_t)i;
                break;
            }
        
        if (index >= 0) {
            HOP_HISTORY[index].visits = visits;
            HOP_HISTORY[index].last_visit = last_visit;
            continue;
        }

        if (grow_history(HOP_HISTORY_COUNT + 1) != 0) {
            fclose(fp);
            return;
        }

        snprintf(HOP_HISTORY[HOP_HISTORY_COUNT].path, sizeof(HOP_HISTORY[HOP_HISTORY_COUNT].path), "%s", path);
        HOP_HISTORY[HOP_HISTORY_COUNT].visits = visits;
        HOP_HISTORY[HOP_HISTORY_COUNT].last_visit = last_visit;
        HOP_HISTORY_COUNT++;
    }

    fclose(fp);
    prune_history();
}

void run_hop(int argc, char **argv) {
    char target[PATH_MAX] = "";

    if (argc == 1) {
        resolve_target("~", target, sizeof(target));
        if (!target[0] || try_hop(target) != 0) {
            puts("hop: no such directory");
        }
        return;
    }

    for (int i = 1; i < argc; i++) {
        const char *name = argv[i];

        if (strcmp(name, ".") == 0)
            continue;

        target[0] = '\0';
        resolve_target(name, target, sizeof(target));

        if (!target[0] || try_hop(target) != 0)
            puts("hop: no such directory");
    }
}

static void resolve_target(const char *name, char *target, size_t size) {
    if (strcmp(name, "~") == 0) {
        const char *home = shell_get_home();
        if (home)
            snprintf(target, size, "%s", home);
        return;
    }
    if (strcmp(name, "-") == 0) {
        if (HAS_PREV)
            snprintf(target, size, "%s", PREV_DIR);
        return;
    }
    if (strcmp(name, "..") == 0 || strcmp(name, ".") == 0) {
        snprintf(target, size, "%s", name);
        return;
    }

    struct stat st;
    if (stat(name, &st) == 0 && S_ISDIR(st.st_mode)) {
        snprintf(target, size, "%s", name);
        return;
    }

    frecency_match(name, target, size);
}

static int frecency_match(const char *name, char *target, size_t size) {
    long long best_score = -1;
    const char *best_path = NULL;

    prune_history();

    for (size_t i = 0; i < HOP_HISTORY_COUNT; i++) {
        if (!strstr(HOP_HISTORY[i].path, name))
            continue;

        long long score = current_history_score(&HOP_HISTORY[i]);
        // update the score only if 
            // 1. first encounter or 
            // 2. target has a higher frecency score or 
            // 3. score is tied and current target comes before entry lexicographically
        if (!best_path || score > best_score || (score == best_score && strcmp(HOP_HISTORY[i].path, best_path) < 0)) {
            best_score = score;
            best_path = HOP_HISTORY[i].path;
        }
    }

    if (!best_path) return 1;

    snprintf(target, size, "%s", best_path);
    return 0;
}

static int try_hop(const char *path) {
    char old_dir[PATH_MAX];
    char resolved[PATH_MAX];

    // save cwd in case hop fails
    if (!getcwd(old_dir, sizeof(old_dir))) return 1;
    if (chdir(path) != 0) return 1;

    if (!getcwd(resolved, sizeof(resolved))) {
        
        if (chdir(old_dir) == 0) return 1;
        return 1;
    }

    HAS_PREV = true;
    snprintf(PREV_DIR, sizeof(PREV_DIR), "%s", old_dir);
    
    int status = record_hop(resolved);
    if (status) perror("hop: failed to persist history");
    
    return 0;
}


void hop_shutdown(void) {
    prune_history();
    
    if (HOP_HISTORY_FILE[0] != '\0' && save_history_file() != 0)
        perror("hop: failed to save history");
    free(HOP_HISTORY);
    
    HOP_HISTORY = NULL;
    HOP_HISTORY_COUNT = 0;
    HOP_HISTORY_CAP = 0;
}


static int grow_history(size_t size) {
    if (size <= HOP_HISTORY_CAP) return 0;

    size_t new_cap = HOP_HISTORY_CAP ? HOP_HISTORY_CAP * 2 : 16;
    while (new_cap < size)
        new_cap *= 2;

    HopEntry *next = realloc(HOP_HISTORY, new_cap * sizeof(*next));
    if (!next)
        return 1;

    HOP_HISTORY = next;
    HOP_HISTORY_CAP = new_cap;
    return 0;
}

static long long current_history_score(const HopEntry *entry) {
    long long recency = entry->last_visit;
    long long frequency = entry->visits * (long long)HOP_VISIT_WEIGHT_SECONDS;
    return frequency + recency;
}

static int save_history_file(void) {
    if (HOP_HISTORY_FILE[0] == '\0') return 0;

    // using fopen instead of open otherwise code will get bulky
    FILE *fp = fopen(HOP_HISTORY_FILE, "w");
    if (!fp) {
        perror("hop: failure opening history");
        return 1;
    }

    for (size_t i = 0; i < HOP_HISTORY_COUNT; i++) {
        int status = fprintf(fp, "%lld\t%lld\t%s\n", HOP_HISTORY[i].visits, HOP_HISTORY[i].last_visit, HOP_HISTORY[i].path);
        if (status < 0) {
            fclose(fp);
            return 1;
        }
    }

    if (fclose(fp) != 0) {
        perror("hop: failure closing history");
        return 1;
    }
    return 0;
}

static void prune_history(void) {
    size_t write_index = 0;

    for (size_t i = 0; i < HOP_HISTORY_COUNT; i++) {
        struct stat st;
        if (stat(HOP_HISTORY[i].path, &st) != 0 || !S_ISDIR(st.st_mode))
            continue;

        if (write_index != i)
            HOP_HISTORY[write_index] = HOP_HISTORY[i];
        write_index++;
    }

    HOP_HISTORY_COUNT = write_index;
}

static int record_hop(const char *path) {
    time_t now = time(NULL);
    if (now == (time_t)-1)
        now = 0;

    prune_history();

    ssize_t index = -1;
    for (size_t i = 0; i < HOP_HISTORY_COUNT; i++)
        if (strcmp(HOP_HISTORY[i].path, path) == 0) {
            index = (ssize_t)i;
            break;
        }
    
    
    if (index >= 0) {
        HOP_HISTORY[index].visits++;
        HOP_HISTORY[index].last_visit = (long long)now;
    } else {
        if (grow_history(HOP_HISTORY_COUNT + 1) != 0)
            return 1;

        snprintf(HOP_HISTORY[HOP_HISTORY_COUNT].path, sizeof(HOP_HISTORY[HOP_HISTORY_COUNT].path), "%s", path);
        HOP_HISTORY[HOP_HISTORY_COUNT].visits = 1;
        HOP_HISTORY[HOP_HISTORY_COUNT].last_visit = (long long)now;
        HOP_HISTORY_COUNT++;
    }

    return save_history_file();
}
