#include <stdlib.h>
#include <stdio.h>

#include "input.h"

int read_user_input(char **out_line, size_t *out_len) {
    char* line = NULL;
    size_t len = 0;
    
    ssize_t n = getline(&line, &len, stdin);

    if (n == -1) {
        
        free(line); line = NULL;
        if (feof(stdin))        // User sent EOF signal
            return 1;            
        return 2;  
    }

    // remove trailing newline
    if (len && line[n-1] == '\n')
        line[--n] = '\0';

    *out_line = line;
    if (out_len) *out_len = (size_t)n;
    
    return 0;
}
