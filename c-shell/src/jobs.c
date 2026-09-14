#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

#include "jobs.h"

typedef struct {
    pid_t pid;
    char *command;
    bool stopped;
} Process;

typedef struct {
    int number;
    pid_t pgid;
    char *command;
    bool isBackground;
    bool abnormal_exit;
    Process *proc_list;
    int count;
} Job;

static Job *jobs;
static int count;
static int next_number = 1;
static volatile sig_atomic_t reap_pending;

static void sigchild_handler(int signal_no) { reap_pending = 1; }

static void remove_job(int job_no)
{
    Job *job = &jobs[job_no];
    for (int i = 0; i < job->count; i++)
        free(job->proc_list[i].command);
    free(job->proc_list);
    free(job->command);

    memmove(&jobs[job_no], &jobs[job_no + 1],
            (size_t)(count - job_no - 1) * sizeof(*jobs));
    count--;
}

void jobs_init(void) {

    struct sigaction sa = {0};
    sa.sa_handler = sigchild_handler;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);

    if (sigaction(SIGCHLD, &sa, NULL) == -1)
        perror("cshell: error installing SIGCHLD handler");
}

void jobs_shutdown(void) {

    for (int i = 0; i < count; i++) {
        for (int j = 0; j < jobs[i].count; j++)
            free(jobs[i].proc_list[j].command);
        free(jobs[i].proc_list);
        free(jobs[i].command);
    }
    free(jobs);
    jobs = NULL;
    count = 0;
}

int add_job(pid_t pgid, const char *command, bool background) {

    char *saved_command = strdup(command);
    if (!saved_command)
        return -1;

    Job *temp = realloc(jobs, (size_t)(count + 1) * sizeof(*jobs));
    if (!temp) {
        free(saved_command);
        return -1;
    }

    jobs = temp;
    jobs[count] = (Job){
        .number = next_number++,
        .pgid = pgid,
        .command = saved_command,
        .isBackground = background,
    };
    return count++;
}

int job_number(int job_no) {
    if (job_no >= 0 && job_no < count)
        return jobs[job_no].number;
    
    return -1;
}

int add_process(int job_no, pid_t pid, const char *command)
{
    if (job_no < 0 || job_no >= count)
        return 1;

    char *saved_command = strdup(command);
    if (!saved_command)
        return 1;

    Job *job = &jobs[job_no];
    Process *temp = realloc(job->proc_list, (size_t)(job->count + 1) * sizeof(*job->proc_list));
    if (!temp) {
        free(saved_command);
        return 1;
    }

    job->proc_list = temp;
    job->proc_list[job->count++] = (Process){
        .pid = pid,
        .command = saved_command,
    };
    return 0;
}

void jobs_reap_and_report(void)
{
    if (!reap_pending)
        return;

    reap_pending = 0;

    int status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        bool handled = false;

        for (int i = 0; i < count && !handled; i++) {
            Job *job = &jobs[i];
            for (int j = 0; j < job->count; j++) {
                if (job->proc_list[j].pid != pid)
                    continue;

                if (WIFSIGNALED(status))
                    job->abnormal_exit = true;

                free(job->proc_list[j].command);
                memmove(&job->proc_list[j], &job->proc_list[j + 1],
                        (size_t)(job->count - j - 1) * sizeof(*job->proc_list));
                job->count--;

                if (job->count == 0) {
                    if (job->isBackground) {
                        printf("%s with pid %ld exited %s\n", job->command,
                               (long)job->pgid,
                               job->abnormal_exit ? "abnormally" : "normally");
                        fflush(stdout);
                    }
                    remove_job(i);
                }

                handled = true;
                break;
            }
        }
    }
}
