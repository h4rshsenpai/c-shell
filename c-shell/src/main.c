#include <stdio.h>
#include <stdlib.h>

#include "exec.h"
#include "hop.h"
#include "input.h"
// #include "jobs.h"
#include "lexer.h"
#include "parser.h"
#include "prompt.h"

int main(void) {
    char *input = NULL;
    size_t len = 0;
    Token *tokens = NULL;
    CommandLine *cmd = NULL;
    
    shell_init();
    hop_init();
    // jobs_init();

    while (1) {
        print_prompt();

        int status = read_user_input(&input, &len);
        
        if (status == 1) {   // User pressed Ctrl+D
            puts("Logging out");
            
            hop_shutdown();
            free(input);    
            return 0;
        }
        if (status == 2) {  // read_user_input failed
            perror("cshell: getline failed during user input");
            
            free(input); input = NULL;
            continue;
        }

        // --> if empty input, go back to shell prompt
        if (len == 0) { 
            free(input); input = NULL;
            continue;
        }

        // --> pass input to lexer
        ssize_t n_tokens = tokenize(input, &tokens);

        if (n_tokens == -1) {  // invalid syntax 
            
            puts("cshell: invalid syntax");
            free(input); input = NULL;
            continue;
        }
        if (n_tokens == -2) {   // malloc error 
            perror("cshell: out of memory");
            hop_shutdown();
            free(input); input = NULL;
            return 1;
        }

        // --> pass tokens to parser which builds an intermediate representation for exec 
            // free tokens since no longer needed
        int parse_status = run_parser(tokens, (size_t)n_tokens, &cmd);

        free_tokens(tokens, (size_t)n_tokens);
        tokens = NULL;

        if (parse_status == 1) {    // invalid grammar

            puts("cshell: invalid syntax");
            free(cmd); cmd = NULL;
            continue;
        }
        if (parse_status == 2) {    // run_parser failed
            perror("cshell: out of memory");
            hop_shutdown();
            free(cmd); cmd = NULL;
            return 1;
        }
        
        // grammer is valid
        // --> parser passes command model to exec
        execute_command_line(cmd);
        free_parsed_command(cmd);
        cmd = NULL;

        free(input);
        input = NULL;
    }
    return 0;
}
    
