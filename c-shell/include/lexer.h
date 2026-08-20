#ifndef LEXER_H
#define LEXER_H

extern const char *whitespace;
extern const char *special;

typedef enum {
    WORD,
    OP_PIPE,    // --> '|z'
    OP_AMP,     // --> '&'
    OP_SEMI,    // --> ';'
    OP_LT,      // --> '<'
    OP_GT,      // --> '>'
    OP_GTGT,    // --> '>>'

    NA,          // placeholder for NULL 
} TokenType;

typedef struct {
    TokenType type;
    char* body;     // only meaningful if type == WORD

} Token;

ssize_t tokenize(const char* p, Token** out_tokens);

void free_tokens(Token* tokens, size_t count);

void helper_trim(const char** p);

int append_token(Token* tok, Token** out_tokens, size_t* count);

int read_word(Token* tok, const char** p);

int read_op(Token* tok, const char** p);

#endif  // LEXER_H
