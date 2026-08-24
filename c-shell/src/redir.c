#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>

#include "redir.h"

int write_all(int fd, const char *buffer, size_t count) {
    size_t written = 0;

    while (written < count) {
        ssize_t n = write(fd, buffer + written, count - written);
        if (n < 0) {
            // add error detection 
            return 1;
        }
        written += (size_t)n;
    }

    return 0;
}

void worker_feed(int *input_fds, int n_ins, int pipe_write_fd) {
    char buf[4096];

    for (int i = 0; i < n_ins; i++) {
        ssize_t n;

        while ((n = read(input_fds[i], buf, sizeof(buf))) > 0) {
            if (write_all(pipe_write_fd, buf, (size_t)n) != 0) {
                close(input_fds[i]);
                close(pipe_write_fd);
                free(input_fds);
                _exit(1);
            }
        }

        close(input_fds[i]);
    }

    close(pipe_write_fd);
    free(input_fds);
    _exit(0);
}

void worker_consume(int *output_fds, int n_outs, int pipe_read_fd) {
    char buf[4096];

    while (true) {
        ssize_t n = read(pipe_read_fd, buf, sizeof(buf));
        if (n <= 0) {
            break;
        }

        for (int i = 0; i < n_outs; i++) {
            if (write_all(output_fds[i], buf, (size_t)n) != 0) {
                close(pipe_read_fd);
                for (int j = 0; j < n_outs; j++) {
                    close(output_fds[j]);
                }
                free(output_fds);
                _exit(1);
            }
        }
    }

    close(pipe_read_fd);
    for (int i = 0; i < n_outs; i++) {
        close(output_fds[i]);
    }
    free(output_fds);
    _exit(0);
}
