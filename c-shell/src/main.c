#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

#include "prompt.h"
#include "input.h"
#include "lexer.h"
#include "parser.h"
#include "exec.h"

int main() {

    Token *tokens = NULL;
    Command *cmd = NULL;
    char *line = NULL; size_t len;

    shell_init();   // initializes shell environment
                    // sets username, hostname and HOME 
                
    while(1) {
        /* ---------- Part A : SHELL INPUT ------------ 

        1. Display shell prompt
        2. Consume input from user
        3. Parse input
            --> Lexer tokenizes raw input and validates syntax
            --> if valid, passes tokens to Parser
                --> parser validates grammar + builds command representation simultaneously
                --> parsed command is sent for execution 
        */

        print_prompt();
        read_status_t status = read_user_input(&line, &len); // removes trailing newline          
        
        if (status == 1) {  // User exits via Ctrl-D    
            puts("Logging out"); 
            exit(0);
        }
        if (status == 2) {  // getline() failed --> try again?
            fprintf(stderr, "getline failure");
            continue;
        } 
        
        // display prompt again if input is empty
        if (len == 0) { free(line); continue; } 

        ssize_t n = tokenize(line, &tokens);
        if (n == -1) {  
            puts("cshell: invalid syntax\n");
            free(line); line = NULL; 
            
            continue;

        } if (n == -2)
            perror("cshell: malloc failure during parsing\n"); 
            exit(1); 
        }
    
        // start an empty command chain; call parser to validate grammer and build command
        // if any error, run_parser frees partially-built command before returning here
            
        int isValid = run_parser(tokens, n, &cmd);
        free_tokens(tokens, n); // no longer needed

        if (isValid != 0) { 
            free(line);
            
            if (isValid == 1) {
                puts("cshell: invalid syntax\n");
                continue;
            } 
            fprintf(stderr, "cshell: out of memory\n");
            exit(1);
        }

        execute_command_group(cmd);
        
        free_command_group(cmd);
        free(line);
    }

    return 0;
}
