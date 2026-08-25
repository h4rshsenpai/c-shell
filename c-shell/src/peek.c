#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "peek.h"
#include "redir.h"

static int read_input(int fd, char **data, size_t *size) {
    char buf[1024];
    size_t cap = 0;
    ssize_t n;

    *data = NULL;
    *size = 0;

    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        if (*size + (size_t)n > cap) {
            cap = cap ? cap * 2 : 1024;
            while (cap < *size + (size_t)n) cap *= 2;

            char *temp = realloc(*data, cap);
            if (!temp) {
                free(*data); *data = NULL;
                return 1;
            }
            *data = temp;
        }

        memcpy(*data + *size, buf, n);
        *size += n;
    }

    if (n < 0) {
        free(*data);
        *data = NULL;
        return 1;
    }

    return 0;
}

static void print_contents(const char *data, size_t size, bool n_flag, bool r_flag, int *line_no) {
    size_t start;
    size_t end;

    if (!r_flag) {
        start = 0;
        for (size_t i = 0; i < size; i++) {
            if (data[i] == '\n') {
                if (n_flag && i > start)
                    printf("%d ", (*line_no)++);
                write_all(STDOUT_FILENO, data + start, i - start + 1);
                start = i + 1;
            }
        }
        if (start < size) {
            if (n_flag) printf("%d ", (*line_no)++);
            write_all(STDOUT_FILENO, data + start, size - start);
        }
        return;
    }

    end = size;
    if (end > 0 && data[end - 1] == '\n') end--;

    while (end > 0) {
        start = end;
        while (start > 0 && data[start - 1] != '\n') start--;
        if (n_flag && end > start)
            printf("%d ", (*line_no)++);
        write_all(STDOUT_FILENO, data + start, end - start);
        write_all(STDOUT_FILENO, "\n", 1);

        if (start == 0) return;
        end = start - 1;
    }
}

static int reverse_regular_file(int fd, bool n_flag, int *line_no) {
    char chunk[1024];
    char *line = NULL;
    size_t line_size = 0;
    size_t line_cap = 0;
    off_t filesize = lseek(fd, 0, SEEK_END);
    off_t pos = filesize;
    bool skip_newline = false;

    if (filesize < 0) return 1;

    while (pos > 0) {
        size_t count;
        if (pos < (off_t)sizeof(chunk))
            count = (size_t)pos;
        else 
            count = sizeof(chunk);
        
        pos -= (off_t)count;

        if (lseek(fd, pos, SEEK_SET) < 0) {
            free(line);
            return 1;
        }

        ssize_t n = read(fd, chunk, count);
        if (n < 0) {
            if (errno == EINTR) {
                pos += (off_t)count;
                continue;
            }
            free(line);
            return 1;
        }

        for (ssize_t i = n - 1; i >= 0; i--) {
            char c = chunk[i];
            off_t fwd_pos = pos + i;

            if (!skip_newline && c == '\n' && fwd_pos == filesize - 1) {
                skip_newline = true;
                continue;
            }
            skip_newline = true;

            if (c == '\n') {
                if (n_flag && line_size > 0)
                    printf("%d ", (*line_no)++);
                for (size_t j = line_size; j > 0; j--)
                    write_all(STDOUT_FILENO, line + j - 1, 1);
                write_all(STDOUT_FILENO, "\n", 1);
                line_size = 0;
                continue;
            }

            if (line_size == line_cap) {
                line_cap = line_cap ? line_cap * 2 : 1024;
                char *temp = realloc(line, line_cap);
                if (!temp) {
                    free(line);
                    return 1;
                }
                line = temp;
            }
            line[line_size++] = c;
        }
    }

    if (line_size > 0) {
        if (n_flag) printf("%d ", (*line_no)++);
        for (size_t j = line_size; j > 0; j--)
            write_all(STDOUT_FILENO, line + j - 1, 1);
        write_all(STDOUT_FILENO, "\n", 1);
    }

    free(line);
    return 0;
}

void run_peek(int argc, char **argv) {
    bool n_flag = false;
    bool r_flag = false;
    int first_file = 1;
    int line_no = 1;

    for (; first_file < argc && argv[first_file][0] == '-'; first_file++) {
        char *flag = argv[first_file] + 1;

        if (*flag == '\0') break;

        while (*flag != '\0') {
            if (*flag == 'n') n_flag = true;
            else if (*flag == 'r') r_flag = true;
            else {
                puts("peek: invalid syntax");
                return;
            }
            flag++;
        }
    }

    if (first_file == argc) {
    // read from standard input since no arguments are passed
        char *data = NULL;
        size_t size = 0;

        if (read_input(STDIN_FILENO, &data, &size) != 0) {
            perror("peek: read error");
            return;
        }

        print_contents(data, size, n_flag, r_flag, &line_no);
        free(data);
        return;
    }


    for (int i = first_file; i < argc; i++) {
        char *data = NULL;
        size_t size = 0;
        
        // read from standard input 
        if (strcmp(argv[i], "-") == 0) {
            
            if (read_input(STDIN_FILENO, &data, &size) != 0) {
                perror("peek: read error");
                return;
            }
            print_contents(data, size, n_flag, r_flag, &line_no);
            free(data);
            continue;
        }

        // files specified, use stat to check for valid paths 
        int fd = open(argv[i], O_RDONLY);
        if (fd < 0) {
            puts("peek: no such file or directory");
            return;
        }
            // is it a directory?
        struct stat info;
        fstat(fd, &info);
        if (S_ISDIR(info.st_mode)) {
            puts("peek: is a directory");
            return;
        }

        if (r_flag) {
            int status = reverse_regular_file(fd, n_flag, &line_no);
            if (status) {
                perror("peek: read error");
                close(fd);
                continue;
            }
        }

        int status = read_input(fd, &data, &size);
        if (status) {
            perror("peek: read error");
            close(fd);
            return;
        }
        close(fd);

        print_contents(data, size, n_flag, r_flag, &line_no);
        free(data);
    }
}
