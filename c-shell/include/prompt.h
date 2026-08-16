#ifndef PROMPT_H
#define PROMPT_H

#include <stddef.h>

// initialize home directory from where shell.out is invoked
// set host name and user name
void shell_init(void);

// get home directory from cache
const char* shell_get_home(void);

// resolve an absolute path to relative from home
void get_cwd_relative(char* path, size_t sz);

void print_prompt(void);

#endif // PROMPT_H
