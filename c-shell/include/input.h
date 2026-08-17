#ifndef INPUT_H
#define INPUT_H

#include <stddef.h> 

typedef enum { READ_OK = 0, READ_EOF = 1, READ_ERROR = -1 } read_status_t;

// consume user input from terminal, return appropriate error codes
read_status_t read_user_input(char **out_line, size_t *out_len);

#endif // INPUT_H
