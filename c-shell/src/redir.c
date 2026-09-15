#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>

#include "redir.h"
#include "parser.h"

int write_all(int fd, const char *buffer, size_t count) {
    size_t written = 0;

    while (written < count) {
        ssize_t n = write(fd, buffer + written, count - written);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return 1;
        }
        if (n == 0)
            return 1;
        written += (size_t)n;
    }

    return 0;
}

void worker_feed(int *input_fds, int n_ins, int pipe_write_fd) {
    char buf[4096];

    for (int i = 0; i < n_ins; i++) {
        ssize_t n;

        while (1) {
            n = read(input_fds[i], buf, sizeof(buf));
            if (n < 0 && errno == EINTR)
                continue;
            if (n <= 0)
                break;

            if (write_all(pipe_write_fd, buf, (size_t)n) != 0) {
                close(input_fds[i]);
                close(pipe_write_fd);
                free(input_fds);
                _exit(1);
            }
        }

        if (n < 0) {
            close(input_fds[i]);
            close(pipe_write_fd);
            free(input_fds);
            _exit(1);
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
        if (n < 0 && errno == EINTR)
            continue;
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

void close_stage_io(StageIO *io) {

    if (io->stdin_fd != -1) {
        close(io->stdin_fd);
        io->stdin_fd = -1;
    }
    if (io->stdout_fd != -1) {
        close(io->stdout_fd);
        io->stdout_fd = -1;
    }
    return;
}

void close_all_pipes(int (*pipes)[2], int pipe_count) {
    for (int i = 0; i < pipe_count; i++) {
        if (pipes[i][0] != -1) {
            close(pipes[i][0]);
            pipes[i][0] = -1;
        }
        if (pipes[i][1] != -1) {
            close(pipes[i][1]);
            pipes[i][1] = -1;
        }
    }
}

int prepare_stage_io(const SimpleCommand *cmd, int (*pipes)[2], int pipe_count, StageIO *io) {

    // open all input file specified for redirection
    // create feeder processes if needed

    if (cmd->n_ins > 0) {
        
        int *input_fds = malloc((size_t)cmd->n_ins * sizeof(*input_fds));
        if (!input_fds) {
            close_stage_io(io);
            close_all_pipes(pipes, pipe_count);

            return 2;
        }

        for (int i = 0; i < cmd->n_ins; i++) {
            input_fds[i] = open(cmd->ins[i], O_RDONLY);

            if (input_fds[i] == -1) {
                puts("cshell: no such file or directory");
                
                for (int j = 0; j < i; j++) 
                    close(input_fds[j]);
                free(input_fds);
                close_stage_io(io);
                close_all_pipes(pipes, pipe_count);

                return 1;
            }
        }

        int feeder_pipe[2];
        if (pipe(feeder_pipe) == -1) {
            perror("cshell: feeder pipe failed");
            
            for (int i = 0; i < cmd->n_ins; i++)
                close(input_fds[i]);
            free(input_fds);
            close_stage_io(io);
            close_all_pipes(pipes, pipe_count);

            return 2;
        }

        // feeder process reads from all input files and write to feeder_pipe[1]
        // command reads from feeder_pipe[0] as its stdin
            
        io->feeder_pid = fork(); 
        
        if (io->feeder_pid < 0) {
            perror("cshell: fork into feeder failed");

            close(feeder_pipe[0]); 
            close(feeder_pipe[1]);
            for (int i = 0; i < cmd->n_ins; i++)
                close(input_fds[i]);    
            free(input_fds);
            close_stage_io(io);
            close_all_pipes(pipes, pipe_count);

            return 2;
        }
        
        if (io->feeder_pid == 0) { 
            
            close(feeder_pipe[0]);
            close_all_pipes(pipes, pipe_count);  // close feeder childs copies of pipes
            if (io->stdout_fd != -1) 
                close(io->stdout_fd);           // close feeder childs copy of stdout
            worker_feed(input_fds, cmd->n_ins, feeder_pipe[1]);
        } 

        io->stdin_fd = feeder_pipe[0];
        
        for (int i = 0; i < cmd->n_ins; i++) 
            close(input_fds[i]);
        close(feeder_pipe[1]);
        free(input_fds);
    }

    if (cmd->n_outs > 0) {
         // open all output files specified for redirection
        // create consumer processes if needed
    
        int *output_fds = malloc((size_t)cmd->n_outs * sizeof(*output_fds));
        int consumer_pipe[2];

        if (!output_fds) {
            close_stage_io(io);
            close_all_pipes(pipes, pipe_count);
            return 2;
        }

        for (int i = 0; i < cmd->n_outs; i++) {

            int append_mode = cmd->outs[i].append ? O_APPEND : O_TRUNC;
            output_fds[i] = open(cmd->outs[i].path, O_WRONLY | O_CREAT | append_mode, 0644);
            
            if (output_fds[i] == -1) {
                puts("cshell: unable to create file for writing");

                for (int j = 0; j < i; j++) 
                    close(output_fds[j]);
                free(output_fds);
                close_stage_io(io);
                close_all_pipes(pipes, pipe_count);  
                return 1;
            }
        }

        if (pipe(consumer_pipe) == -1) {
            perror("cshell: consumer pipe failed");
            
            for (int i = 0; i < cmd->n_outs; i++) 
                close(output_fds[i]);
            free(output_fds);    
            close_stage_io(io);
            close_all_pipes(pipes, pipe_count);
            return 2;
        }

        io->consumer_pid = fork();

        if (io->consumer_pid < 0) {
            perror("cshell: fork into consumer failed");
            close(consumer_pipe[0]);
            close(consumer_pipe[1]);
            for (int i = 0; i < cmd->n_outs; i++)
                close(output_fds[i]);
            
            close_stage_io(io);
            close_all_pipes(pipes, pipe_count);
            free(output_fds);
            return 2;
        }

        if (io->consumer_pid == 0) {    
            close(consumer_pipe[1]);
            close_all_pipes(pipes, pipe_count);
            if (io->stdin_fd != -1) 
                close(io->stdin_fd);

            worker_consume(output_fds, cmd->n_outs, consumer_pipe[0]);

        } 
        
        io->stdout_fd = consumer_pipe[1];
        
        for (int i = 0; i < cmd->n_outs; i++)
            close(output_fds[i]);
        close(consumer_pipe[0]);
        free(output_fds);
    }

    return 0;
}