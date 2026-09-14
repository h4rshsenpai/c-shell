#ifndef _JOBS_H
#define _JOBS_H

#include <stdbool.h>
#include <sys/types.h>

void jobs_init(void);
void jobs_shutdown(void);
int add_job(pid_t pgid, const char *command, bool background);
int job_number(int job);
int add_process(int job, pid_t pid, const char *command);
void jobs_reap_and_report(void);

#endif // _JOBS_H
