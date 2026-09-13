#ifndef _JOBS_H
#define _JOBS_H

#include <stdbool.h>
#include <sys/types.h>

void jobs_init(void);
// void jobs_shutdown(void);
int jobs_add(pid_t pgid, bool background);
int jobs_add_process(int job, pid_t pid, const char *command);
// void jobs_mark_stopped(int job);
// void jobs_mark_running(int job);
// void jobs_reap_and_report(void);
// void jobs_print(void);
// bool jobs_has_stopped(void);
// bool jobs_known_pid(pid_t pid);
// bool jobs_known_number(int number);
// int jobs_signal_target(const char *target, int signal_number);

#endif // _JOBS_H
