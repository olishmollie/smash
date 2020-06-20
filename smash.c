#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/fcntl.h>
#include <sys/param.h>
#include <unistd.h>

#include <readline/history.h>
#include <readline/readline.h>

#define EOS '\0'
#define MAXCHAR 1024
#define MAXARGS 1024

/* Charbuf stores the characters used in token charbuf. */
char charbuf[MAXCHAR];

/* OLDPWD stores the previously visited directory . */
char OLDPWD[MAXPATHLEN];

typedef enum error {
    ERR_SUCCESS,
    ERR_UNEXPECTED_CHAR,
    ERR_UNEXPECTED_TOKEN,
    ERR_TOO_MANY_ARGS,
    ERR_SYMBOL_TABLE_OVERFLOW
} error;

typedef struct token {
    enum {
        TOKTYPE_EOF,
        TOKTYPE_DLR,
        TOKTYPE_LPAREN,
        TOKTYPE_RPAREN,
        TOKTYPE_LBRACE,
        TOKTYPE_RBRACE,
        TOKTYPE_LBRACK,
        TOKTYPE_RBRACK,
        TOKTYPE_DQT,
        TOKTYPE_TILDE,
        TOKTYPE_DOT,
        TOKTYPE_DDOT,
        TOKTYPE_BANG,
        TOKTYPE_STAR,
        TOKTYPE_AMP,
        TOKTYPE_PIPE,
        TOKTYPE_GT,
        TOKTYPE_LT,
        TOKTYPE_DLRPAR,
        TOKTYPE_SYM,
        TOKTYPE_SQSTR,
    } type;
    char *lexeme;
    unsigned int row, col, size;
} token;

/* Global tokens */
token tok_dlr;
token tok_lparen;
token tok_rparen;
token tok_lbrace;
token tok_rbrace;
token tok_lbrack;
token tok_rbrack;
token tok_dqt;
token tok_bang;
token tok_star;
token tok_amp;
token tok_pipe;
token tok_gt;
token tok_lt;
token tok_tilde;
token tok_dot;
token tok_ddot;
token tok_dlrpar;
token tok_eof;
token tok_for;

char *toknames[] = {"TOKTYPE_EOF",    "TOKTYPE_DLR",    "TOKTYPE_LPAREN",
                    "TOKTYPE_RPAREN", "TOKTYPE_LBRACE", "TOKTYPE_RBRACE",
                    "TOKTYPE_LBRACK", "TOKTYPE_RBRACK", "TOKTYPE_DQT",
                    "TOKTYPE_TILDE",  "TOKTYPE_DOT",    "TOKTYPE_DDOT",
                    "TOKTYPE_BANG",   "TOKTYPE_STAR",   "TOKTYPE_AMP",
                    "TOKTYPE_PIPE",   "TOKTYPE_GT",     "TOKTYPE_LT",
                    "TOKTYPE_DLRPAR", "TOKTYPE_SYM",    "TOKTYPE_SQSTR"};

void token_debug(token *tok) {
    char *name = toknames[tok->type];
    printf("{ type: %s, lexeme: '%s' }\n", name, tok->lexeme);
}

/**
 * Tokstream represents the state of the lexer. It stores the input to be
 * parsed, a char buffer for lexemes, and its current position.
 */
typedef struct tokstream {
    struct {
        char *charbuf;
        unsigned int pos;
    } symtable;
    char *input;
    unsigned int start, end;
} tokstream;

int is_space(unsigned int c) {
    switch (c) {
    case ' ':
    case '\t':
    case '\v':
    case '\n':
        return 1;
    }
    return 0;
}

// TODO: -- Just look up the ascii codes why dontcha?
int is_symbol_char(unsigned int c) {
    switch (toupper(c)) {
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9':
    case '0':
    case 'A':
    case 'B':
    case 'C':
    case 'D':
    case 'E':
    case 'F':
    case 'G':
    case 'H':
    case 'I':
    case 'J':
    case 'K':
    case 'L':
    case 'M':
    case 'N':
    case 'O':
    case 'P':
    case 'Q':
    case 'R':
    case 'S':
    case 'T':
    case 'U':
    case 'V':
    case 'W':
    case 'X':
    case 'Y':
    case 'Z':
    case '.':
    case '/':
    case '-':
    case '~':
        return 1;
    }
    return 0;
}

int char_isdelim(unsigned int c) {
    if (is_space(c)) {
        return 1;
    }

    return c == EOS || c == '$' || c == '&' || c == '|' || c == '<' ||
           c == '>' || c == '(' || c == ')' || c == '"' || c == '{';
}

int tokstream_eof(tokstream *ts) {
    return ts->end >= strlen(ts->input);
}

int peek(tokstream *ts) {
    return ts->input[ts->end];
}

int next(tokstream *ts) {
    return ts->input[ts->end++];
}

int ignore(tokstream *ts) {
    ts->start = ++ts->end;
    return ts->input[ts->end];
}

int backup(tokstream *ts) {
    return ts->input[--ts->end];
}

int tokstream_error(tokstream *ts, int err) {
    int c = ts->input[ts->end];

    switch (err) {
    case ERR_UNEXPECTED_CHAR:
        fprintf(stderr, "read error: unexpected token '%c%s'\n", c,
                c ? "" : "EOF");
        break;
    case ERR_SYMBOL_TABLE_OVERFLOW:
        fprintf(stderr, "read error: symbol table overflow\n");
        break;
    default:
        fprintf(stderr, "read error: unknown error code %d\n", err);
    }

    return err;
}

/**
 * Add `lexeme` to the symbol table and populate `tok` size, lexeme
 * (a pointer into the symbol table), and type. If overflow occurs, return
 * an error code and set tok->lexeme to NULL.
 */
int token_init(tokstream *ts, token *tok, int type, char *lexeme, int len) {
    tok->lexeme = ts->symtable.charbuf + ts->symtable.pos;
    tok->size = len;
    tok->type = type;

    for (int i = 0; i < len; ++i) {
        if (ts->symtable.pos >= MAXCHAR) {
            tok->lexeme = NULL;
            return tokstream_error(ts, ERR_SYMBOL_TABLE_OVERFLOW);
        }

        ts->symtable.charbuf[ts->symtable.pos++] = *lexeme++;
    }

    ts->symtable.charbuf[ts->symtable.pos++] = '\0';

    return ERR_SUCCESS;
}

/**
 * Initialize a tokstream's symbol table with keywords and
 * symbols, give it an input to parse, and reset the state of the lexer.
 */
int tokstream_init(tokstream *ts, char *input) {

    ts->symtable.charbuf = charbuf;
    ts->symtable.pos = 0;

    /*
     * NOTE: -- We don't check for errors here because the only error token_init
     * can return is due to overflow. As long as MAXCHAR is defined as greater
     * than the number of preallocated tokens, it shouldn't pose a problem.
     */
    token_init(ts, &tok_dlr, TOKTYPE_DLR, "$", 1);
    token_init(ts, &tok_lparen, TOKTYPE_LPAREN, "(", 1);
    token_init(ts, &tok_rparen, TOKTYPE_RPAREN, ")", 1);
    token_init(ts, &tok_lbrace, TOKTYPE_LBRACE, "{", 1);
    token_init(ts, &tok_rbrace, TOKTYPE_RBRACE, "}", 1);
    token_init(ts, &tok_lbrack, TOKTYPE_LBRACK, "[", 1);
    token_init(ts, &tok_rbrack, TOKTYPE_RBRACK, "]", 1);
    token_init(ts, &tok_dqt, TOKTYPE_DQT, "\"", 1);
    token_init(ts, &tok_bang, TOKTYPE_BANG, "!", 1);
    token_init(ts, &tok_star, TOKTYPE_STAR, "*", 1);
    token_init(ts, &tok_amp, TOKTYPE_AMP, "&", 1);
    token_init(ts, &tok_pipe, TOKTYPE_PIPE, "|", 1);
    token_init(ts, &tok_gt, TOKTYPE_GT, ">", 1);
    token_init(ts, &tok_lt, TOKTYPE_LT, "<", 1);
    token_init(ts, &tok_dlrpar, TOKTYPE_DLRPAR, "$(", 2);
    token_init(ts, &tok_eof, TOKTYPE_EOF, "EOF", 3);

    ts->start = 0;
    ts->end = 0;
    ts->input = input;

    return ERR_SUCCESS;
}

/**
 * Parse ts's input and assigns tok to the next token in the
 * stream.
 */
int tokstream_next(tokstream *ts, token *tok) {
    char c = peek(ts);

    while (!tokstream_eof(ts) && is_space(c)) {
        c = ignore(ts);
    }

    if (is_symbol_char(c)) {
        while (!char_isdelim(c)) {
            c = next(ts);
        }
        backup(ts);

        return token_init(ts, tok, TOKTYPE_SYM, ts->input + ts->start,
                          ts->end - ts->start);
    }

    int err = ERR_SUCCESS;
    switch (c) {
    case '\'':
        c = ignore(ts);

        while (c && c != '\'') {
            c = next(ts);
        }
        backup(ts);

        if (c != '\'') {
            err = tokstream_error(ts, ERR_UNEXPECTED_CHAR);
            break;
        }

        err = token_init(ts, tok, TOKTYPE_SQSTR, ts->input + ts->start,
                         ts->end - ts->start);

        /* Ignore the closing quote. */
        ignore(ts);

        break;
    case '"':
        *tok = tok_dqt;
        ignore(ts);
        break;
    case '(':
        *tok = tok_lparen;
        ignore(ts);
        break;
    case ')':
        *tok = tok_rparen;
        ignore(ts);
        break;
    case '<':
        *tok = tok_lt;
        ignore(ts);
        break;
    case '>':
        *tok = tok_gt;
        ignore(ts);
        break;
    case '|':
        *tok = tok_pipe;
        ignore(ts);
        break;
    case '&':
        *tok = tok_amp;
        ignore(ts);
        break;
    case '!':
        *tok = tok_bang;
        ignore(ts);
        break;
    case '*':
        *tok = tok_star;
        ignore(ts);
        break;
    case '{':
        *tok = tok_lbrace;
        ignore(ts);
        break;
    case '}':
        *tok = tok_rbrace;
        ignore(ts);
        break;
    case '[':
        *tok = tok_lbrack;
        ignore(ts);
        break;
    case ']':
        *tok = tok_rbrack;
        ignore(ts);
        break;
    case '~':
        *tok = tok_tilde;
        ignore(ts);
        break;
    case '.':
        ignore(ts);

        if (peek(ts) == '.') {
            *tok = tok_ddot;
            ignore(ts);
        } else {
            *tok = tok_dot;
        }

        break;
    case '$':
        ignore(ts);

        if (peek(ts) == '(') {
            *tok = tok_dlrpar;

            ignore(ts);
        } else {
            *tok = tok_dlr;
        }

        break;
    case EOS:
    case EOF:
        *tok = tok_eof;
        break;
    default:
        err = tokstream_error(ts, ERR_UNEXPECTED_CHAR);
    }

    return err;
}

/**
 * Command represents a shell directive. It stores a directive's pid, argument
 * list, any redirection information, and whether the process is to be executed
 * in the background.
 */
typedef struct command {
    int background;
    unsigned int pid;

    enum { COMTYPE_STANDARD, COMTYPE_BUILTIN, COMTYPE_PIPE } type;

    char *in, *out;

    unsigned int argc;
    char *argv[MAXCHAR];
} command;

int command_init(command *c, int type) {
    c->type = type;
    c->in = NULL;
    c->out = NULL;
    c->argc = 0;
    c->background = 0;
    c->pid = 0;
    return ERR_SUCCESS;
}

char *builtins[] = {"cd", "exit"};
unsigned int nbuiltins = sizeof(builtins) / sizeof(builtins[0]);

int is_builtin(char *sym) {
    for (unsigned int i = 0; i < nbuiltins; ++i) {
        if (strcmp(sym, builtins[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

int tok_isdelim(token *tok) {
    return tok->type == TOKTYPE_DLR || tok->type == TOKTYPE_DLRPAR ||
           tok->type == TOKTYPE_GT || tok->type == TOKTYPE_LT ||
           tok->type == TOKTYPE_AMP || tok->type == TOKTYPE_EOF;
}

int parse_error(token *tok, int err) {
    switch (err) {
    case ERR_UNEXPECTED_TOKEN:
        fprintf(stderr, "parse error: unexpected token '%s'\n", tok->lexeme);
        break;
    case ERR_TOO_MANY_ARGS:
        fprintf(stderr, "parse error: too many arguments\n");
        break;
    /* These two are handled by the tokstream. */
    case ERR_SYMBOL_TABLE_OVERFLOW:
    case ERR_UNEXPECTED_CHAR:
        break;
    default:
        fprintf(stderr, "parse error: unknown error code %d\n", err);
    }

    return err;
}

int expect(tokstream *ts, token *tok, int toktype) {
    int err = tokstream_next(ts, tok);
    if (tok->type != toktype) {
        return ERR_UNEXPECTED_TOKEN;
    }

    return err;
}

int parse_redirects(command *c, tokstream *ts, token *tok) {
    int err = ERR_SUCCESS;

    while (tok->type == TOKTYPE_LT || tok->type == TOKTYPE_GT) {
        if (tok->type == TOKTYPE_LT) {
            err = expect(ts, tok, TOKTYPE_SYM);
            if (err != ERR_SUCCESS) {
                return err;
            }

            c->in = tok->lexeme;
        } else {
            err = expect(ts, tok, TOKTYPE_SYM);
            if (err != ERR_SUCCESS) {
                return err;
            }

            c->out = tok->lexeme;
        }

        err = tokstream_next(ts, tok);
        if (err != ERR_SUCCESS) {
            return err;
        }
    }

    return err;
}

int parse_command(command *c, tokstream *ts, token *tok) {
    int err;

    err = command_init(c, COMTYPE_STANDARD);
    if (err) {
        return err;
    }

    err = tokstream_next(ts, tok);
    if (err) {
        return err;
    }

    if (is_builtin(tok->lexeme)) {
        c->type = COMTYPE_BUILTIN;
    }

    c->argv[c->argc++] = tok->lexeme;

    while ((err = tokstream_next(ts, tok)) == 0 && !tok_isdelim(tok)) {
        if (c->argc >= MAXARGS) {
            return ERR_TOO_MANY_ARGS;
        }

        c->argv[c->argc++] = tok->lexeme;
    }

    if (err != 0) {
        return err;
    }

    c->argv[c->argc] = (char *)NULL;

    return parse_redirects(c, ts, tok);
}

/**
 * Parse input into a command object. If something goes wrong, it
 * returns an error code. Otherwise, it returns ERR_SUCCESS.
 */
int parse(command *c, char *input) {
    tokstream ts;
    token tok;
    int err;

    tokstream_init(&ts, input);

    err = parse_command(c, &ts, &tok);
    if (err) {
        return parse_error(&tok, err);
    }

    if ((err = expect(&ts, &tok, TOKTYPE_EOF))) {
        return parse_error(&tok, err);
    }

    return ERR_SUCCESS;
}

int exec_builtin(command *c) {
    if (strcmp(c->argv[0], "exit") == 0) {
        int arg = 0;
        if (c->argc > 1) {
            // NOTE: not checking for errors in atoi, it might blow up.
            int arg = atoi(c->argv[1]);
        }
        exit(arg);
    }

    if (strcmp(c->argv[0], "cd") == 0) {
        // Save OLDPWD into tmp var.
        getcwd(OLDPWD, MAXPATHLEN);

        // Switch directories.
        if (c->argc > 1) {
            if (strcmp(c->argv[1], "-") == 0) {
                chdir(OLDPWD);
            } else {
                chdir(c->argv[1]);
            }
        } else {
            chdir("$HOME");
        }
    }

    if (strcmp(c->argv[0], "jobs") == 0) {
        // TODO
    }

    return ERR_SUCCESS;
}

int exec_standard(command *c) {
    if (c->in) {
        // Open the destination file and rewire stdout.
        int ifd = open(c->in, O_RDWR, 0644);

        dup2(ifd, 0);
        close(ifd);
    }

    if (c->out) {
        // Open the source file and rewire stdin.
        int ofd = open(c->out, O_RDWR | O_CREAT | O_TRUNC, 0644);

        dup2(ofd, 1);
        close(ofd);
    }

    if (execvp(c->argv[0], c->argv) == -1) {
        fprintf(stderr, "%s: ", c->argv[0]);

        switch (errno) {
        case EPERM:
            fprintf(stderr, "operation not permitted\n");
            break;
        case ENOENT:
            fprintf(stderr, "no such file or directory\n");
            break;
        case EACCES:
            fprintf(stderr, "permission denied\n");
            break;
        default:
            fprintf(stderr, "unknown errno: %d\n", errno);
        }
    }

    return errno;
}

void print_space(int n) {
    for (int i = 0; i < n; ++i) {
        printf(" ");
    }
}

void command_debug(command *c, int space) {
    print_space(space);
    printf("Command {\n");

    int i;
    switch (c->type) {
    case COMTYPE_STANDARD:
        print_space(space + 4);
        printf("type: COMTYPE_STANDARD\n");

        print_space(space + 4);
        printf("name: %s\n", c->argv[0]);

        print_space(space + 4);
        printf("args: [");
        for (i = 1; i < c->argc; ++i) {
            printf("'%s'%s", c->argv[i], i != c->argc - 1 ? ", " : "");
        }
        printf("]\n");

        print_space(space + 4);
        printf("in: %s\n", c->in);

        print_space(space + 4);
        printf("out: %s\n", c->out);

        print_space(space + 4);
        printf("background: %s\n", c->background ? "true" : "false");

        break;
    case COMTYPE_BUILTIN:
        print_space(space + 4);
        printf("type: COMTYPE_BUILTIN\n");

        print_space(space + 4);
        printf("name: %s\n", c->argv[0]);

        print_space(space + 4);
        printf("args: [");
        for (i = 1; i < c->argc; ++i) {
            printf("'%s'%s", c->argv[i], i != c->argc - 1 ? ", " : "");
        }
        printf("]\n");

        print_space(space + 4);
        printf("in: %s\n", c->in);

        print_space(space + 4);
        printf("out: %s\n", c->out);

        print_space(space + 4);
        printf("background: %s\n", c->background ? "true" : "false");

        break;
    case COMTYPE_PIPE:
        // TODO
        break;
    }

    print_space(space);
    printf("}\n");
}

int run(char *input) {
    command c;
    int err, pid;

    err = parse(&c, input);
    if (err != 0) {
        return err;
    }

    if (c.type == COMTYPE_BUILTIN) {
        return exec_builtin(&c);
    }

    pid = fork();
    if (pid < 0) {
        fprintf(stderr, "error forking process\n");
        exit(1);
    }

    if (pid == 0) {
        exit(exec_standard(&c));
    }

    c.pid = pid;
    waitpid(c.pid, 0, 0);

    return ERR_SUCCESS;
}

int main() {
    char *prompt, *input;

    while (1) {
        prompt = "Smash $ ";
        input = readline(prompt);

        if (input && strlen(input) > 0) {
            add_history(input);

            run(input);
        }

        /* Exit on ^D */
        if (input == NULL) {
            break;
        }

        // Valgrind doesn't like using delete with the c malloc from readline.
        free(input);
    }

    printf("\n");

    return 0;
}
