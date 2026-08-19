#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

#include "prompt.h"
#include "input.h"
#include "lexer.h"

int main() {


    shell_init();
    
    while(1) {

        // display shell prompt
        print_prompt();
        
        // take input from user
        char *line; size_t len;
        read_status_t status = read_user_input(&line, &len); // removes trailing newline               
       
        // printf("%s", line);

        if (status == READ_ERROR) {
            return 1;
        }
        if (status == READ_EOF) {   // Ctrl-D EOF 
            puts("Logging out");
            exit(1);
        }

        // status == READ_OK, line is valid
        if (len == 0) { free(line); continue; } // empty input is valid

        // pass input to lexer
        Token* tokens; 
        ssize_t n = tokenize(line, &tokens);
        
        if (n < 0) {                    // syntax error or out of memory
            free(line);
            if (n == -1) { 
                puts("cshell: invalid syntax\n");
                continue;
            }
            
            fprintf(stderr, "cshell: out of memory\n"); 
            exit(1); 
        } 
        // syntax valid, now n holds number of tokens retrieved
        // pass to parser
        
        printf("%ld tokens read:\n", n);
        for (int i=0; i<n; i++) 
            printf("%d %s\n", tokens[i].type, tokens[i].body);
        
        // run_cmd(parse_cmd(line))
    }

    return 0;
}
