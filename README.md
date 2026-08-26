# C-Shell assumptions

## Part A: Shell Input

### A1 - prompt + input loop

- initialize shell state once at startup
- print prompt
- read one line from stdin
- if EOF, print logout message and exit
- if empty input, skip execution and print prompt again

### A2 - lex + parse + exec flow

- tokenize raw input into shell tokens
- reject invalid syntax early in lexer/parser
- build a `CommandLine` containing pipelines, with each pipeline containing `SimpleCommand` stages
- store each stage's arguments and input/output redirections separately
- pass the parsed command model to the exec layer
- free temporary lexer/parser allocations after every command

## Part B: Shell Intrinsics

### B1 - hop

- parse arguments from left to right
- if no argument, hop to shell home directory
- for each argument:
  - `.` means stay in same directory
  - `..` means try parent directory
  - `~` means shell home directory
  - `-` means previous successful directory if available
  - otherwise treat argument like a direct relative or absolute path
- on successful `chdir`, update previous directory
- if a target does not resolve directly, search persistent hop history by path substring
- rank history entries by visit frequency and last visit time
- skip history entries whose directories no longer exist
- load hop history when the shell starts and save it when the shell exits
- store history in `.hop_history` under the shell's startup directory

### B2 - reveal

- parse only `-a` and `-t`
- accept at most one path-like argument
- resolve target directory using same path rules as hop, except no frecency lookup
- open directory and read entries
- sort entries lexicographically
- hide dotfiles unless `-a` is set
- if `-t` is set, recurse into subdirectories after printing directory entry

### B3 - peek

- parse only `-n` and `-r`
- if no file arguments, read from stdin
- for each argument:
  - `-` means stdin
  - missing path prints `peek: no such file or directory`
  - directory prints `peek: is a directory`
  - regular file with `-r` uses backward chunk reads with `lseek`
  - stream input with `-r` falls back to buffering and reverse printing
- `-n` numbers only non-empty lines
- when multiple files are given, process them in argument order

### B4 - locate

- reject zero arguments with `locate: invalid syntax`
- for each command name:
  - check current working directory first
  - then scan every directory listed in `PATH`, in order
  - print every executable match as an absolute path
- if no match is found, print `locate: command not found (name)`

## Part C: File Redirection and Pipes

### C1 - command execution

- execute `hop` in the parent shell when it is a standalone foreground command so directory changes persist
- execute `peek`, `locate`, and `reveal` in child processes
- if command is not a builtin, resolve executable path
- if command contains `/`, treat it like a literal executable path
- otherwise check current directory first, then `PATH`
- `%name` skips current-directory lookup and searches only `PATH`

### C2 - input redirection

- open every input file with `O_RDONLY`
- if any file open fails, print `cshell: no such file or directory`
- if all opens succeed, use a feeder process and private pipe to feed file contents to command stdin in listed order
- close all opened descriptors after setup

### C3 - output redirection

- open every output file in its own mode
- `>` truncates, `>>` appends
- if any output file open fails, print `cshell: unable to create file for writing`
- if setup succeeds, use a consumer process and private pipe to copy command stdout into every listed output target

### C4 - pipes

- create one pipe between every adjacent command stage
- fork one child per stage, plus feeder/consumer helper processes for redirections when needed
- choose each stage's effective stdin and stdout from its redirection or adjacent pipeline endpoint
- wire the selected descriptors with `dup2`
- redirection replaces the corresponding pipeline endpoint for that stage
- close unused pipe ends in every parent, stage child, and helper process
- wait for foreground command and helper processes; leave background processes running and reap finished children later
