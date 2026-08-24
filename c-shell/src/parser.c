#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "lexer.h"
#include "parser.h"

static int parse_pipeline(ParserState *state, Pipeline *out_pipeline);
static int parse_single_command(ParserState *state, SimpleCommand *out_cmd);
static int parse_command_suffix(ParserState *state, SimpleCommand *cmd);
static int append_pipeline(CommandLine *line, Pipeline *pipeline);
static int append_stage(Pipeline *pipeline, SimpleCommand *cmd);
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

static void free_pipeline(Pipeline *pipeline) {
    if (!pipeline) return;

    for (int i = 0; i < pipeline->count; i++) 
        free_single_command(&pipeline->stages[i]);

    free(pipeline->stages);
    // zero out heap allocated pipeline 
    memset(pipeline, 0, sizeof(*pipeline));
}

void free_command_line(CommandLine *line) {
    if (!line) return; 

    for (int i = 0; i < line->count; i++) 
        free_pipeline(&line->pipelines[i]);

    free(line->pipelines); line->pipelines = NULL;
    free(line); line = NULL;
}

int run_parser(const Token *tokens, size_t n, CommandLine **out_line) {
    
    CommandLine *line = NULL; *out_line = NULL;
    ParserState state = {tokens, n, 0};
    
    if (n == 0) return 0; // empty input is valid
    
    line = calloc(1, sizeof(*line));
    if (!line) return 2;
    
    while (state.pos < state.count) {
        Pipeline pipeline = {0};
        TokenType separator; 

        int status = parse_pipeline(&state, &pipeline);
        if (status) {
            // invalid grammer or malloc error
            free_pipeline(&pipeline); free_command_line(line);
            return status;
        }

        if(state.pos < state.count && state.tokens[state.pos].type == OP_AMP) {
            pipeline.isBackground = true;
            separator = OP_AMP;
            state.pos++;
        } 
        else if(state.pos < state.count && state.tokens[state.pos].type == OP_SEMI) {
            separator = OP_SEMI;
            state.pos++;
        }
        else { separator = NA;}

    
        // parser checks for trailing ; or & at end of input
        // if present, returns with error immediately and pipeline is not appended
        if (state.pos >= state.count && separator != NA) {
            free_pipeline(&pipeline);
            free_command_line(line);
            return 1; 
        }
        if(append_pipeline(line, &pipeline) != 0) {
            free_pipeline(&pipeline);
            free_command_line(line);
            return 2;
        }
    }    

    // grammer valid -> pass parsed input to main for execution
    *out_line = line;
    return 0;
}

static int parse_pipeline(ParserState *state, Pipeline *out_pipeline) {
    
    while (1) {
        // parse CMD at every stage 
        SimpleCommand stage = {0};

        int status = parse_single_command(state, &stage);
        if (status) {
            free_single_command(&stage);
            return status;
        }
        
        // append parsed CMD to pipeline
        if (append_stage(out_pipeline, &stage) != 0) {
            free_single_command(&stage);
            return 2;
        }

        // end pipeline if EOF or next connector is not '|'
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

static int append_pipeline(CommandLine *line, Pipeline *pipeline) {
    Pipeline *tmp = realloc(line->pipelines, (size_t)(line->count + 1) * sizeof(*line->pipelines));
    if (!tmp)
        return 1;

    line->pipelines = tmp;
    line->pipelines[line->count] = *pipeline;
    line->count++;

    // zero out pipeline to prevent double free 
    memset(pipeline, 0, sizeof(*pipeline));
    return 0;
}


static int append_stage(Pipeline *pipeline, SimpleCommand *cmd) {
    SimpleCommand *tmp = realloc(pipeline->stages, (size_t)(pipeline->count + 1) * sizeof(*pipeline->stages));
    if (!tmp)
        return 1;

    pipeline->stages = tmp;
    pipeline->stages[pipeline->count] = *cmd;
    pipeline->count++;

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
