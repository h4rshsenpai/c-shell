#ifndef REDIR_H
#define REDIR_H

int write_all(int fd, const char *buffer, size_t count);
void worker_feed(int *input_fds, int n_ins, int pipe_write_fd);
void worker_consume(int *output_fds, int n_outs, int pipe_read_fd);

#endif // REDIR_H