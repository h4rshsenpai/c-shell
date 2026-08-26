# POSIX-compliant build for shell.out

CC = gcc
CFLAGS = -std=c23 -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=700 -Wall -Wextra -Werror -Wno-unused-parameter -fno-asm

BUILD_DIR = build
SRC_DIR = src
INC_DIR = include

SRCS = $(SRC_DIR)/main.c \
	   $(SRC_DIR)/prompt.c \
	   $(SRC_DIR)/input.c \
	   $(SRC_DIR)/lexer.c \
	   $(SRC_DIR)/parser.c \
 	   $(SRC_DIR)/exec.c \
	   $(SRC_DIR)/redir.c \
	   $(SRC_DIR)/hop.c \
	   $(SRC_DIR)/locate.c \
	   $(SRC_DIR)/reveal.c \
	   $(SRC_DIR)/peek.c \

OBJS = $(SRCS:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)

INCLUDES = -I$(INC_DIR)

.PHONY: all clean

all: shell.out

shell.out: $(OBJS)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^


# Compile any .c into build/.o; header deps are handled by full rebuilds for simplicity
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

clean:
	rm -rf $(BUILD_DIR) shell.out
