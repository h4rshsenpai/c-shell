#ifndef INPUT_H
#define INPUT_H

#include <stddef.h> 

// consume user input from terminal, return appropriate error codes
int read_user_input(char **out_line, size_t *out_len);

#endif // INPUT_H
