#ifndef REDIR_H
#define REDIR_H

#include <stddef.h>
#include "parser.h"

typedef struct {
    int stdin_fd;
    int stdout_fd;
    pid_t feeder_pid;
    pid_t consumer_pid;
} StageIO;

int write_all(int fd, const char *buffer, size_t count);
void worker_feed(int *input_fds, int n_ins, int pipe_write_fd);
void worker_consume(int *output_fds, int n_outs, int pipe_read_fd);
void close_stage_io(StageIO *io);
void close_all_pipes(int (*pipes)[2], int pipe_count);
int prepare_stage_io(const SimpleCommand *cmd, int (*pipes)[2], int pipe_count, StageIO *io);

#endif // REDIR_H
