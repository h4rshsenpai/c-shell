#ifndef PARSER_H
#define PARSER_H

#include <stdbool.h>
#include <stddef.h>

#include "lexer.h"

typedef struct { char *path; bool append; } Outfile;

typedef struct {
    int argc;
    int n_ins;
    int n_outs;
    char **ins;
    char **argv;
    Outfile *outs;
} SimpleCommand;

typedef struct {
    SimpleCommand *list;
    int count;
    bool isBackground;
} CommandGroup;

typedef struct {
    CommandGroup *list;
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
