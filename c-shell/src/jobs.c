#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "jobs.h"

typedef struct {
    pid_t pid;
    char *command;
    bool stopped;
} Process;

typedef struct {
    int number;
    pid_t pgid;
    bool isBackground;
    bool stopped;
    Process *processes;
    int count;
} Job;

static Job *jobs = NULL;
static int count = 0, next_number = 1;
// static volatile sig_atomic_t child_pending;

int jobs_add(pid_t pgid, bool background) {

    Job *temp = realloc(jobs, (size_t)(count+1)*sizeof(*jobs));
    if (!temp) return -1;

    jobs = temp;
    jobs[count] = (Job) {
        .number = next_number++,
        .pgid = pgid,
        .isBackground = background,
        .stopped = false,
        .processes = NULL,
        .count = 0,
    };

    return count++;
}

int jobs_add_process(int job_no, pid_t pid, const char *command) {
        
    int i = jobs[job_no].count++;
    Process *p = realloc(jobs[job_no].processes, (size_t)(i+1)*sizeof(Process*));
    if (!p) return 1;
    
    p[i] = (Process) {
        .command = strdup(command),
        .pid = pid,
        .stopped = false,
    };

    return 0;
}

