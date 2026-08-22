#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

#include "prompt.h"
#include "input.h"
#include "lexer.h"
#include "parser.h"

int main() {

    shell_init();
    
    while(1) {

        // A1. display shell prompt
        print_prompt();
        
        // A2. take user input
        char *line = NULL; size_t len;
        read_status_t status = read_user_input(&line, &len); // removes trailing newline               
       
        if (status == READ_ERROR) {
            fprintf(stderr, "cshell: failed to read input");
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
        // syntax valid, n holds number of tokens retrieved
        
        // A3. pass to parser
        
        for (int i=0; i<n; i++) 
            printf("%d %s\n", tokens[i].type, tokens[i].body);
        
        Command *cmd = NULL;
        int isValid = run_parser(tokens, n, &cmd);

        // tokens no longer needed
        free_tokens(tokens, n);

        if (isValid != 0) {              // invalid grammer or out of memory 
            // run_parser frees any partial chain implicitly
            free(line);
            
            if (isValid == 1) {
                puts("cshell: invalid syntax\n");
                continue;
            } 
            fprintf(stderr, "cshell: out of memory\n");
            exit(1);
        }
        // input is valid as per grammar, pass to exec  
        run_cmd(cmd);

        free_command_chain(cmd);
        free(line);
    }

    return 0;
}
