#include <errno.h>
#include <stdlib.h>
#include <stdio.h>

#include "input.h"

int read_user_input(char **out_line, size_t *out_len) {
    char* line = NULL;
    size_t len = 0;
    
    ssize_t n = getline(&line, &len, stdin);
    
    // bg process completion can interrupt getline
    // check for EINTR and report INPUT_INTERRUPTED to main loop to handle the interrupt and redraw prompt
    if (n == -1 && errno == EINTR) {
        clearerr(stdin);
        free(line);
        return INPUT_INTERRUPTED;
    }

    if (n == -1) {
        
        free(line); line = NULL;
        if (feof(stdin))        // User sent EOF signal
            return INPUT_EOF;
        return INPUT_ERROR;
    }

    // remove trailing newline
    if (len && line[n-1] == '\n')
        line[--n] = '\0';

    *out_line = line;
    if (out_len) *out_len = (size_t)n;
    
    return INPUT_OK;
}
