#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

#include "input.h"

read_status_t read_user_input(char **out_line, size_t *out_len) {

    char* line = NULL;
    size_t len = 0;
    ssize_t n = getline(&line, &len, stdin);

    if (n < 0) {
        free(line);
        *out_line = NULL;

        if (ferror(stdin)) {
            fprintf(stderr, "failed to read input");
            return READ_ERROR;
        }
        return READ_EOF;
    }

    // trim trailing newline
    if (len && line[n-1] == '\n')
        line[--n] = '\0';

    *out_line = line;
    if (out_len) *out_len = (size_t)n;
    
    return READ_OK;
}
