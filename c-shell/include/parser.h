#ifndef PARSER_H
#define PARSER_H

#include <stdbool.h>
#include <stddef.h>

#include "lexer.h"

typedef struct {
    char *path;
    bool append;
} Outfile;

typedef struct {
    char **argv;
    int argc;

    char **ins;
    int n_ins;

    Outfile *outs;
    int n_outs;
} SimpleCommand;

typedef struct {
    SimpleCommand *stages;
    int count;
    bool isBackground;
} Pipeline;

typedef struct {
    Pipeline *pipelines;
    int count;
} CommandLine;

typedef struct {
    const Token *tokens;
    size_t count;
    size_t pos;
    
} ParserState;

void free_parsed_command(CommandLine *line);
int run_parser(const Token *tokens, size_t n, CommandLine **out_line);

#endif // PARSER_H
