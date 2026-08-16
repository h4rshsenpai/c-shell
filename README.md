# C Shell - Implementing my own C-shell & MLFQ scheduling policy for xv6

Submission 1 - Parts A, B & C
Due Date - 24 Aug

## Part A : Shell Input -- 20 marks

### A1: Shell Prompt -- 3 marks 

- Requirements
1. Resolve absolute paths to relative from /home. Show absolute path if /home isn't ancestor
2. Display prompt only when shell isn't running a foreground process


### A2: User Input -- 2 marks

- Requirements
1. Consume input -> Display prompt again


### A3: Input Parsing -- 15 marks **<--- HARDEST**
#### Rough Flow 

1. **Lexer pass**
    - scan raw input, classify into token stream, apply *maximal munch* and quote/escape rules.
    - Reject early if lexical error (see doc)
    - Output list of tokens to parser.

2. **Parser pass**
    - Walk the outputted token list and maintain at each point:
        - a pointer to the head/tail for the command chain. 
        - a current state variable (`LINE`, `ARG`, `CMD`, etc).
        - a current command pointer the parser will fill at that state
    - For each token, process using `switch` on (state, token_type):
        - if type is `WORD` while in state `ARG`/`LINE`/`CMD`/`BG`, append token to tail command's 
        argv. Change state to `ARG`. 
        - if type is `OP_LT`/`OP_GT`/`OP_GTGT` while in `ARG`, set tail command's redirects. 
        Transition to `TGT`. 
        - and so on.

## Part B: Built-in Commands -- 40 marks

### B1: hop 
### B2: reveal
### B3: pee
### B4: locate
## Part C: File Redirection + Pipes -- 50 marks

### C1: Command Execution -- 8 marks 
### C2: Input Redirection -- 12 marks
### C3: Output Redirection -- 12 marks
### C4: Command Piping -- 18 marks

