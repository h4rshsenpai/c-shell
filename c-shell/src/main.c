#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

#include "prompt.h"
#include "input.h"
#include "parser.h"

int main() {

    shell_init();
    
    while(1) {

        // display shell prompt
        print_prompt();
        
        // take input from user
        char *line; size_t len;
        read_status_t status = read_user_input(&line, &len);                 
        
        if (status == READ_ERROR) {
            // jobs_kill_all();
            // history_shutdown();
            return 1;
        } 
        if (status == READ_EOF) {       // Ctrl-D 
            puts("Logging out");
            // jobs_kill_all();
            // history_shutdown();
        }
        // status == READ_OK, line is valid
        // check for empty prompt
        if (len == 0) { free(line); continue; }

        // pass to parser
        // run_cmd(parse_cmd(line))
    }

    return 0;
}
