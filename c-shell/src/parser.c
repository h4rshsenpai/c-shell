#include <stdlib.h>
#include <string.h>

#include "lexer.h"
#include "parser.h"

static int parse_cmd_group(ParserState *state, CommandGroup *out_cmd_group);
static int parse_single_command(ParserState *state, SimpleCommand *out_cmd);
static int parse_command_suffix(ParserState *state, SimpleCommand *cmd);
static int append_cmd_group(CommandLine *line, CommandGroup *cmd_group);
static int append_stage(CommandGroup *cmd_group, SimpleCommand *cmd);
static int append_word(char ***items, int *n, char *body);
static int append_tgt(TokenType op, SimpleCommand* cmd, char *body);

static void free_single_command(SimpleCommand *cmd) {
    if (!cmd) return;

    for (int i = 0; i < cmd->argc; i++) free(cmd->argv[i]);
    free(cmd->argv); cmd->argv = NULL;

    for (int i = 0; i < cmd->n_ins; i++) free(cmd->ins[i]);
    free(cmd->ins); cmd->ins = NULL;

    for (int i = 0; i < cmd->n_outs; i++) free(cmd->outs[i].path);
    free(cmd->outs); cmd->outs = NULL;

    // zero out heap allocated cmd for sanity 
    memset(cmd, 0, sizeof(*cmd));
}

static void free_cmd_group(CommandGroup *cmd_group) {
    if (!cmd_group) return;

    for (int i = 0; i < cmd_group->count; i++) 
        free_single_command(&cmd_group->list[i]);

    free(cmd_group->list);
    // zero out heap allocated cmd_group 
    memset(cmd_group, 0, sizeof(*cmd_group));
}

void free_parsed_command(CommandLine *line) {
    if (!line) return; 

    for (int i = 0; i < line->count; i++) 
        free_cmd_group(&line->list[i]);

    free(line->list); line->list = NULL;
    free(line); line = NULL;
}

int run_parser(const Token *tokens, size_t n, CommandLine **out_line) {
    
    CommandLine *line = NULL; *out_line = NULL;
    ParserState state = {tokens, n, 0};
    
    if (n == 0) return 0; // empty input is valid
    
    line = calloc(1, sizeof(*line));
    if (!line) return 2;
    
    while (state.pos < state.count) {
        CommandGroup cmd_group = {0};
        TokenType separator; 

        int status = parse_cmd_group(&state, &cmd_group);
        if (status) {
            // invalid grammer or malloc error
            free_cmd_group(&cmd_group); free_parsed_command(line);
            return status;
        }

        if(state.pos < state.count && state.tokens[state.pos].type == OP_AMP) {
            cmd_group.isBackground = true;
            separator = OP_AMP;
            state.pos++;
        } 
        else if(state.pos < state.count && state.tokens[state.pos].type == OP_SEMI) {
            separator = OP_SEMI;
            state.pos++;
        }
        else { separator = NA;}

    
        // A trailing ampersand backgrounds the final cmd_group; a trailing
        // semicolon has no command on its right and is invalid.
        if (state.pos >= state.count && separator == OP_SEMI) {
            free_cmd_group(&cmd_group);
            free_parsed_command(line);
            return 1; 
        }
        if(append_cmd_group(line, &cmd_group) != 0) {
            free_cmd_group(&cmd_group);
            free_parsed_command(line);
            return 2;
        }
    }    

    // grammer valid -> pass parsed input to main for execution
    *out_line = line;
    return 0;
}

static int parse_cmd_group(ParserState *state, CommandGroup *out_cmd_group) {
    
    while (1) {
        // parse CMD at every stage 
        SimpleCommand stage = {0};

        int status = parse_single_command(state, &stage);
        if (status) {
            free_single_command(&stage);
            return status;
        }
        
        // append parsed CMD to cmd_group
        if (append_stage(out_cmd_group, &stage) != 0) {
            free_single_command(&stage);
            return 2;
        }

        // end cmd_group if EOF or next connector is not '|'
        if (state->pos >= state->count || state->tokens[state->pos].type != OP_PIPE)
            return 0;   

        state->pos++;

        // invalid grammer if parser reaches leaf and 
        if (state->pos >= state->count)
            return 1;
    }
}

// CMD --> WORD ARG 
static int parse_single_command(ParserState *state, SimpleCommand *out_cmd) {
    // expects WORD
    if (state->pos >= state->count || state->tokens[state->pos].type != WORD)
        return 1;

    char *word = strdup(state->tokens[state->pos].body);
    if (!word) return 2;

    if(append_word(&out_cmd->argv, &out_cmd->argc, word)) { free(word); return 2;}
    
    state->pos++;
    return parse_command_suffix(state, out_cmd);
}

// ARG --> 
static int parse_command_suffix(ParserState *state, SimpleCommand *cmd) {
   
    // keep consuming WORD recursively until redirection operators 
    while (state->pos < state->count) {
        Token cur = state->tokens[state->pos];
        char *copy = NULL;

        // ARG --> WORD ARG
        if(cur.type == WORD) {
            copy = strdup(cur.body); if (!copy) return 2;
            
            if (append_word(&cmd->argv, &cmd->argc, copy)) { free(copy); return 2; }
            
            state->pos++;
            continue;
        }
        
        if(cur.type == OP_LT || cur.type == OP_GT || cur.type == OP_GTGT) {

            // TGT expected, return with error if nothing
            if ((state->pos + 1) >= state->count || state->tokens[state->pos+1].type != WORD)
                return 1;
            
            copy = strdup(state->tokens[state->pos + 1].body); if (!copy) return 2;
            
            if (append_tgt(cur.type, cmd, copy) != 0) {
                    free(copy);
                    return 2;
            }
            
            state->pos += 2;
            continue;
        }
        break;
    }
    return 0;
}

static int append_cmd_group(CommandLine *line, CommandGroup *cmd_group) {
    CommandGroup *tmp = realloc(line->list, (size_t)(line->count + 1) * sizeof(*line->list));
    if (!tmp)
        return 1;

    line->list = tmp;
    line->list[line->count] = *cmd_group;
    line->count++;

    // zero out cmd_group to prevent double free 
    memset(cmd_group, 0, sizeof(*cmd_group));
    return 0;
}


static int append_stage(CommandGroup *cmd_group, SimpleCommand *cmd) {
    SimpleCommand *tmp = realloc(cmd_group->list, (size_t)(cmd_group->count + 1) * sizeof(*cmd_group->list));
    if (!tmp)
        return 1;

    cmd_group->list = tmp;
    cmd_group->list[cmd_group->count] = *cmd;
    cmd_group->count++;

    // zero out cmd to prevent double free
    memset(cmd, 0, sizeof(*cmd));
    return 0;
}


static int append_word(char ***items, int *n, char *body) {
    
    char **temp = realloc(*items, (size_t)(*n + 2) * sizeof(**items));  // one for token, one for NULL
    if (!temp) return 1;
    
    *items = temp;
    (*items)[*n] = body; (*n)++;
    (*items)[*n] = NULL;

    return 0;
}

static int append_tgt(TokenType op, SimpleCommand* cmd, char *body) {
    if (op == OP_LT) {
        char **temp = realloc(cmd->ins, (size_t)(cmd->n_ins + 2) * sizeof(*cmd->ins));
        if (!temp) 
            return 1;
        
        cmd->ins = temp;
        cmd->ins[cmd->n_ins] = body; 
        cmd->n_ins++;
        cmd->ins[cmd->n_ins] = NULL; 
        
        return 0;
    }

    // tgt is output file
    bool isAppend = ( op == OP_GTGT);
    Outfile *temp = realloc(cmd->outs, (size_t)(cmd->n_outs + 1) * sizeof(*cmd->outs));
    if (!temp)         
        return 1;


    cmd->outs = temp;
    cmd->outs[cmd->n_outs] = (Outfile){body, isAppend};
    cmd->n_outs++;
    return 0;

}
