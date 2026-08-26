#include <stdlib.h>
#include <string.h>

#include "lexer.h"

#define BUFFER_SIZE 1024
const char *whitespace = " \t\n\r";
const char *special = "|&;<>";

void helper_trim(const char** p) {    
    while (*p && **p != '\0' && strchr(whitespace, **p) != NULL) ++(*p);
}

int append_token(Token* tok, Token** out_tokens, size_t *count) {
    size_t new_count = *count + 1;
    Token *temp = realloc(*out_tokens, new_count * sizeof(Token));
    
    if (temp == NULL)
        return 1;

    *out_tokens = temp;
    (*out_tokens)[*count] = *tok; 
    *count = new_count;

    return 0;
}

void free_tokens(Token* tokens, size_t count) {
    if (!tokens) return;
    
    // only WORD tokens carry body, rest are NULL so free doesn't do anything
    for (size_t i=0; i < count; i++) 
        free(tokens[i].body);
    
    free(tokens);
}

// TODO ---- add extra flags for error checking 
ssize_t tokenize(const char* p, Token **out_tokens) {
    size_t count = 0;  // tracks number of valid tokens
    int status;         // return value for append_token, read_word and read_op

    *out_tokens = NULL;

    helper_trim(&p);
    while (p && *p != '\0') {
        Token tok;
        tok.type = NA; tok.body = NULL;

        if (strchr(special, *p) != NULL) // special character : '|', '&', '>', '<'. 
            status = read_op(&tok, &p);
        else                            // only possibility is WORD
            status = read_word(&tok, &p);

        if (status != 0) {  // 1 - invalid syntax, 2 - memory error
            free_tokens(*out_tokens, count);
            *out_tokens = NULL;
            return -1*status;
        }

        status = append_token(&tok, out_tokens, &count);
        if (status) {   // realloc issue
            free(tok.body);
            free_tokens(*out_tokens, count);
            *out_tokens = NULL;
            return -2;            
        }
        helper_trim(&p);
    }
    return count;
}

int read_word(Token* tok, const char** p) {
    char word[BUFFER_SIZE] = "";
    int len = 0;
    
    int dq_open = 0, sq_open = 0;
    while (*p && **p != '\0') {

        if (strchr(whitespace, **p) && !dq_open && !sq_open)
            break; 

        else if (strchr(special, **p)) {
            if (dq_open || sq_open) { word[len++] = **p; (*p)++; }
            else break;       // special character outside quotes -> end WORD
        } 

        else if (**p == '\'') { sq_open = !sq_open; (*p)++; }
        
        else if (**p == '"') { dq_open = !dq_open; (*p)++; }
        
        else if (**p == '\\') {         // escape 
            word[len++] = **p; (*p)++; 
 
            if (**p == '\0')
                return 1;       // trailing '\' -> invalid syntax
            else if (!dq_open && !sq_open) { word[len-1] = **p; (*p)++; }
            else if (sq_open) continue;
            else {          
                // only possibility is inside double quotes 
                if (**p == '\\' || **p == '"') {  word[len-1] = **p; (*p)++; } 
                else { word[len++] = **p; (*p)++; }
            }
        }
        // only possibility is ordinary character
        else { word[len++] = **p; (*p)++; }
    }
    if (sq_open || dq_open)
        return 1;       // unclosed quote -> invalid syntax

    tok->type = WORD;
    tok->body = strdup(word);
    if (!tok->body)
            return 2;
    return 0;
} 

int read_op(Token* tok, const char** p) {

    switch(**p) {
        case '|' : 
            tok->type = OP_PIPE; ++(*p);
            break;
    
        case '&' : 
            tok->type = OP_AMP; ++(*p);
            break;
        
        case ';' :
            tok->type = OP_SEMI; ++(*p);
            break;

        case '<' : 
            tok->type = OP_LT; ++(*p);
            break;
        
        case '>' : 
            // apply maximal munch
            ++(*p);
            if (**p == '>') { tok->type = OP_GTGT; ++(*p); }
            else { tok->type = OP_GT; }
            break;
        
        // useless default, read_op is called iff *p is special character, 
        default : 
            return 1;
    }
    
    return 0; 
}
