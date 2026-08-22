#ifndef PARSER_H
#define PARSER_H

#include <stdbool.h>
#include <stddef.h>

#include "lexer.h"

typedef struct {
    char *path;
    bool append;    // true if OP_GTGT
} Outfile;

typedef struct Command {
    char **argv;             
    int argc;   
    
    char **ins;   
    int n_ins;
    Outfile *outs;
    int n_outs;

    bool isBackground;

    struct Command *next;   // command after '|' or ';', or NULL
    char connector;         // '|', ';', '\0' if no command follows
} Command;


int consume_and_next(Token tok, Command *cmd, size_t *pos);

void free_command_group(Command *head); 

int run_parser(const Token *tokens, size_t n, Command **out_cmd);

int parse_cmd(const Token *tokens, size_t n, size_t *pos, Command **out_cmd);

int parse_arg(const Token *tokens, size_t n, size_t *pos, Command *cmd);

int parse_bg(const Token *tokens, size_t n, size_t *pos, Command *cmd);

int parse_tgt(const Token *tokens, size_t n, size_t *pos, Command *cmd);


#endif // PARSER_H

