#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "lexer.h"
#include "parser.h"

static int consume_and_next(Token tok, Command *cmd, size_t* pos);
static void apply_background_to_group(Command *cmd);
static int parse_cmd(const Token *tokens, size_t n, size_t *pos, Command **out_cmd);
static int parse_arg(const Token *tokens, size_t n, size_t *pos, Command **out_cmd);
static int parse_bg(const Token *tokens, size_t n, size_t *pos, Command **out_cmd)
static int parse_tgt(const Token *tokens, size_t n, size_t *pos, Command **out_cmd)


/* typedef struct Command {
    char **argv;
    int argc;
    char **ins;
    int n_ins;
    Outfile *outs;
    int n_outs;
    bool isBackground;
    struct Command *next;
    char connector;         
*/

void free_command_group(Command *head) {
    while (head) {
        Command *next = head->next;
        
        for (int i = 0; i < head->argc; i++) free(head->argv[i]);
        free(head->argv);
        
        for (int i = 0; i < head->n_ins; i++) free(head->ins[i]);
        free(head->ins);
        
        for (int i = 0; i < head->n_outs; i++) free(head->outs[i].path);
        free(head->outs);

        free(head);
        head = next;
    }
}

int run_parser(const Token *tokens, size_t n, Command **out_cmd) {
    if (n == 0) 
        return 0; // empty input valid
    
    int status;
    size_t pos = 0;
    status = parse_cmd(tokens, n, &pos, out_cmd);
    
    // "ls -l | wc > file.txt &"  -- '&' applies to the entire command not just wc 
    if (status == 0)
        apply_background_to_group(*out_cmd); // push isBackground up the chain
    return status;
}

static int parse_cmd(const Token *tokens, size_t n, size_t *pos, Command **out_cmd) {
        
    if (*pos >= n || tokens[*pos].type != WORD)  
        return 1; 
 
    Command *next_cmd = (Command *)calloc(1, sizeof(Command));
    if (!next_cmd) 
        return 2;  
    
    // 1. consume WORD
        // if consume fails (malloc error), free the entire chain 
    if (consume_and_next(tokens[*pos], next_cmd, pos)) { 
        
        // if consume fails, free the entire chain built uptil now
        free_command_group(next_cmd);
        *out_cmd = NULL;
        return 2; 
    }

    // 2. add command to chain, node links are handled by parse_arg
    *out_cmd = next_cmd; 

    // 3. parse ARG
    int status = parse_arg(tokens, n, pos, next_cmd);
    if (status != 0) {
        // free this node and anything parse_arg linked below it
        // deeper nodes that failed are expected to be freed by their own parse_cmd
        free_command_group(next_cmd);
        return status;
    }
    return 0;
}

static int parse_arg(const Token *tokens, size_t n, size_t *pos, Command *cmd) {
    
    if (*pos >= n)
        return 0; // ε case - nothing follows

    int status;
    Command *next_cmd = NULL; // only meaningful if complex command
    switch(tokens[*pos].type) {

        case WORD:
            while (*pos < n && tokens[*pos].type == WORD) // WORD can recurse into WORD so while loop optimizes
                //                                       // recursion depth to O(no of command) instead of O(no of tokens)
                if (consume_and_next(tokens[*pos], cmd, pos))
                    return 2;
            return parse_arg(tokens, n, pos, cmd);

        case OP_LT:
        case OP_GT:
        case OP_GTGT:
            return parse_tgt(tokens, n, pos, cmd);
        
        case OP_PIPE:
            (*pos)++; // consume '|' 
            
            cmd->connector = '|';
            
            status = parse_cmd(tokens, n, pos, &next_cmd);
            if (status) 
                return status;
            
            cmd->next = next_cmd;
            return 0;

        case OP_SEMI:
            (*pos)++; // consume ';' 
            
            cmd->connector = ';';

            // Command *next_cmd = NULL;
            status = parse_cmd(tokens, n, pos, &next_cmd);
            if (status) 
                return status;
            
            cmd->next = next_cmd;
            return 0;
        
        case OP_AMP:
            (*pos)++; // consume '&'
            return parse_bg(tokens, n, pos, cmd);
        
        default:
            return 1;
    }
}

static int parse_bg(const Token *tokens, size_t n, size_t *pos, Command *cmd) {

    if (*pos >= n) {        // ε case - nothing follows
        cmd->next = NULL;
        return 0;
    }  
    
    // WORD ARG case -> new command
    Command *next_cmd;
    int status = parse_cmd(tokens, n, pos, &next_cmd);
    if (status)
        return status;
    
    cmd->connector = '&';
    cmd->next = next_cmd;
    return 0;
}

static int parse_tgt(const Token *tokens, size_t n, size_t *pos, Command *cmd) {
    TokenType op = tokens[*pos].type;
    (*pos)++;   // consume <, >, or >>
    
    if (*pos >= n || tokens[*pos].type != WORD)
        return 1;

    // copy WORD - filename/path for redirection
    char *copy = strdup(tokens[*pos].body); 
    if (!copy) 
        return 2;

    if (op == OP_LT) {
        char **temp = realloc(cmd->ins, (cmd->n_ins + 2) * sizeof(char*));  // **ins must be NULL terminated 
        if (!temp) return 2;

        cmd->ins = temp;
        cmd->ins[cmd->n_ins] = copy;
        cmd->n_ins++;
        cmd->ins[cmd->n_ins] = NULL;
    }
    else {
        Outfile *temp = realloc(cmd->outs, (cmd->n_outs + 2) * sizeof(Outfile));
        if (!temp) return 2;
        
        cmd->outs = temp;

        cmd->outs[cmd->n_outs].path = copy;
        cmd->outs[cmd->n_outs].append = (op == OP_GTGT);
        
        cmd->n_outs++; 
        cmd->outs[cmd->n_outs].path = NULL;
    }

    ++(*pos);       // consume target WORD

    return parse_arg(tokens, n, pos, cmd);
}

static int consume_and_next(Token tok, Command *cmd, size_t* pos) {
    
    char **temp = realloc(cmd->argv, (cmd->argc + 2) * sizeof(char*));  // one for token, one for NULL
    if (!temp) return 2;
    cmd->argv = temp; 

    char *copy = strdup(tok.body);
    if (!copy) return 2;
    
    cmd->argv[cmd->argc] = copy; 
    cmd->argc++;
    cmd->argv[cmd->argc] = NULL;
    (*pos)++;
    
    return 0;
}

static void apply_background_to_group(Command *cmd) {
    Command *group_head = cmd;

    while (cmd) {
        if (cmd->connector == '&') {
            Command *current = group_head;

            while (current) {
                current->isBackground = true;

                if (current == cmd)
                    break;
                current = current->next;
            }

            group_head = cmd->next;
        }
        else if (cmd->connector == ';')
            group_head = cmd->next;

        cmd = cmd->next;
    }
}
