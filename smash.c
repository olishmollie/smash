/* smash.c
 *
 * Author: AJ Bond
 * Date: 01/14/2021
 *
 * Notes:
 * SMASH, a STudent MAde SHell, is a basic unix shell written in C89 that
 * supports pipes, input and output redirection, and background processes.
 */

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/fcntl.h>
#include <sys/param.h>
#include <sys/wait.h>
#include <unistd.h>

#define MAXARGS 64
#define MAXJOBS 256

void panic(char *msg) {
    fprintf(stderr, "%s\n", msg);
    exit(1);
}

typedef struct tok_s {
    enum {
        TK_EOF = EOF,
        TK_AMP = '&',
        TK_PIPE = '|',
        TK_DLR = '$',
        TK_GT = '>',
        TK_LT = '<',
        TK_SEMI = ';',
        TK_NWLN = '\n',
        TK_SYM = 256,
    } type;
    char *lexeme;
    int size;
} tok_t;

tok_t mk_token(int type, char *lexeme, int size) {
    tok_t t;

    t.type = type;

    if (t.type == TK_SYM) {
        if (!(t.lexeme = malloc(size + 1))) {
            panic("mk_tok: out of memory");
        }
        strcpy(t.lexeme, lexeme);
    } else {
        t.lexeme = NULL;
    }

    t.size = size;

    return t;
}

typedef struct com_s {
    int bg, argc;
    pid_t pid;
    char *infile, *outfile;
    char *argv[MAXARGS];

    struct com_s *next;
} com_t;

com_t *new_command(void) {
    com_t *c;
    if (!(c = malloc(sizeof(com_t)))) {
        panic("new_command: out of memory");
    }
    c->bg = c->argc = 0;
    c->pid = 0;
    c->infile = c->outfile = NULL;
    c->next = NULL;
    return c;
}

void free_command(com_t *c) {
    int i;
    free(c->infile);
    free(c->outfile);
    for (i = 0; i < c->argc; ++i) {
        free(c->argv[i]);
    }
    free(c);
}

void print_command(com_t *c) {
    int i;
    printf("%s", c->argv[0]);
    for (i = 1; i < c->argc; ++i) {
        printf(" %s", c->argv[i]);
    }
    printf("\n");
}

void print_spaces(int n) {
    while (n-- > 0) {
        printf(" ");
    }
}

#ifdef DEBUG
void command_debug_recursive(com_t *c, int num_spaces, int indent_width) {
    int i;

    if (c == NULL) {
        return;
    }

    printf("com_t {\n");

    print_spaces(num_spaces + indent_width);
    printf("pid: %d\n", c->pid);

    print_spaces(num_spaces + indent_width);
    printf("name: %s\n", c->argv[0]);

    if (c->bg) {
        print_spaces(num_spaces + indent_width);
        printf("bg: true\n");
    }

    if (c->infile) {
        print_spaces(num_spaces + indent_width);
        printf("infile: %s\n", c->infile);
    }

    if (c->outfile) {
        print_spaces(num_spaces + indent_width);
        printf("outfile: %s\n", c->outfile);
    }

    print_spaces(num_spaces + indent_width);
    printf("args: [");

    for (i = 1; i < c->argc; ++i) {
        printf("'%s'", c->argv[i]);
        if (i != c->argc - 1) {
            printf(", ");
        }
    }
    printf("]\n");

    if (c->next) {
        print_spaces(num_spaces + indent_width);
        printf("next: ");
        command_debug_recursive(c->next, num_spaces + indent_width,
                                indent_width);
    }

    print_spaces(num_spaces);
    printf("}");

    if (num_spaces) {
        printf("\n");
    }
}

void command_debug(com_t *c) {
    command_debug_recursive(c, 0, 4);
    printf("\n");
}
#endif

struct {
    int is_eof, is_pipe, jobp;
    com_t *jobs[MAXJOBS];
} shell;

void init_shell(void) {
    shell.is_eof = shell.is_pipe = shell.jobp = 0;
}

int fpeek(FILE *stream) {
    int c = getc(stream);
    ungetc(c, stream);
    return c;
}

int isop(int c) {
    return c == '&' || c == '|' || c == '>' || c == '<';
}

int isdelim(int c) {
    return c == '\n' || c == ';';
}

int isspace(int c) {
    return c == ' ' || c == '\t' || c == '\v';
}

tok_t comment(FILE *in) {
    int c = fgetc(in);
    while (c != '\n' && c != EOF) {
        c = fgetc(in);
    }
    return mk_token(c, NULL, 1);
}

tok_t eof(void) {
    shell.is_eof = 1;
    return mk_token(TK_EOF, NULL, 1);
}

tok_t symbol(FILE *in, int c, char *lexeme, int pos);

tok_t quote(FILE *in, int c, char *lexeme, int pos) {
    int delim = c;

    while ((c = fgetc(in)) != delim) {
        lexeme[pos++] = c;
        if (c == '\n') {
            printf("quote> ");
        }
    }

    c = fpeek(in);
    return symbol(in, c, lexeme, pos);
}

tok_t symbol(FILE *in, int c, char *lexeme, int pos) {
    while (!isdelim(c) && !isspace(c) && !isop(c)) {
        if (c == EOF) {
            break;
        } else if (c == '\'' || c == '"') {
            return quote(in, fgetc(in), lexeme, pos);
        } else {
            lexeme[pos++] = fgetc(in);
        }
        c = fpeek(in);
    }
    lexeme[pos] = '\0';

    return mk_token(TK_SYM, lexeme, strlen(lexeme));
}

tok_t start(FILE *in, char c) {
    char lexeme[MAXPATHLEN];
    int pos = 0;

    while (isspace(c)) {
        fgetc(in);
        c = fpeek(in);
    }

    if (c == '#') {
        return comment(in);
    } else if (isdelim(c) || isop(c)) {
        fgetc(in);
        return mk_token(c, NULL, 1);
    } else if (c == EOF) {
        return eof();
    } else {
        return symbol(in, c, lexeme, pos);
    }
}

tok_t next_tok(FILE *in) {
    int c = fpeek(in);
    shell.is_eof = 0;
    return start(in, c);
}

void error(char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
}

int isdelim_token(tok_t *t) {
    return t->type == TK_NWLN || t->type == TK_SEMI;
}

com_t *directive(FILE *in, tok_t *curtok) {
    com_t *c;

    if ((isdelim_token(curtok) && !shell.is_pipe) || shell.is_eof) {
        if (curtok->type == TK_SEMI) {
            next_tok(in);
        }
        return NULL;
    } else if (curtok->type == TK_NWLN && shell.is_pipe) {
        printf("pipe> ");
        *curtok = next_tok(in);
    }

    if (curtok->type != TK_SYM) {
        error("unexpected token '%c', expected symbol", (char)curtok->type);
        return NULL;
    }

    c = new_command();
    c->argv[c->argc++] = curtok->lexeme;

    *curtok = next_tok(in);
    while (curtok->type == TK_SYM) {
        if (c->argc > MAXARGS) {
            error("too many arguments");
            return NULL;
        }
        c->argv[c->argc++] = curtok->lexeme;
        *curtok = next_tok(in);
    }
    c->argv[c->argc] = NULL;

    return c;
}

com_t *redirection(FILE *in, tok_t *curtok) {
    com_t *c = directive(in, curtok);

    if (curtok->type == TK_LT) {
        *curtok = next_tok(in);

        if (curtok->type != TK_SYM) {
            error("unexpected token, expected symbol");
            return NULL;
        }

        c->infile = curtok->lexeme;
        *curtok = next_tok(in);
    }

    if (curtok->type == TK_GT) {
        *curtok = next_tok(in);

        if (curtok->type != TK_SYM) {
            error("unexpected token, expected symbol");
            return NULL;
        }

        c->outfile = curtok->lexeme;
        *curtok = next_tok(in);
    }

    return c;
}

com_t *parse(FILE *in);

com_t *pipeline(FILE *in, tok_t *curtok) {
    com_t *c = redirection(in, curtok);

    while (curtok->type == TK_PIPE) {
        *curtok = next_tok(in);
        shell.is_pipe = 1;
        c->next = pipeline(in, curtok);
    }

    shell.is_pipe = 0;

    return c;
}

com_t *parse(FILE *in) {
    tok_t curtok = next_tok(in);
    com_t *c = pipeline(in, &curtok);

    if (curtok.type == TK_AMP) {
        c->bg = 1;
    }

    if (curtok.type != TK_AMP && curtok.type != TK_SEMI &&
        curtok.type != TK_NWLN && curtok.type != TK_EOF) {
        error("unexpected token, expected delimeter");
        return NULL;
    }

    return c;
}

void exec_cd(com_t *c) {
    errno = 0;
    if (c->argc > 1 && chdir(c->argv[1]) == -1) {
        perror("cd");
    }
    free_command(c);
}

void exec_exit(com_t *c) {
    int status = 0;
    if (c->argc > 1) {
        status = atoi(c->argv[1]);
    }
    free_command(c);
    exit(status);
}

void exec_jobs(com_t *c) {
    int i;
    for (i = 0; i < shell.jobp; ++i) {
        printf("[%d]  %c running    ", i + 1, i == shell.jobp - 1 ? '+' : '-');
        print_command(shell.jobs[i]);
    }
    free_command(c);
}

void redirect_input(com_t *c) {
    int fdin;

    errno = 0;
    if ((fdin = open(c->infile, O_RDONLY)) == -1) {
        perror("redirect_input: open");
        return;
    }

    if (dup2(fdin, STDIN_FILENO) == -1) {
        perror("redirect_input: dup2");
        return;
    }

    close(fdin);
}

void redirect_output(com_t *c) {
    int fdout;

    errno = 0;
    if ((fdout = open(c->outfile, O_RDWR | O_CREAT | O_TRUNC, 0644)) == -1) {
        perror("redirect_output: open");
        return;
    }

    if (dup2(fdout, STDOUT_FILENO) == -1) {
        perror("redirect_output: dup2");
        return;
    }

    close(fdout);
}

void exec_pipeline(com_t *c) {
    int pipefd[2], stdin_cpy;
    com_t *p = c;

    stdin_cpy = dup(STDIN_FILENO);

    while (p) {
        errno = 0;

        /* If p isn't first, attach stdin to the pipe. */
        if (p != c) {
            if (dup2(pipefd[0], STDIN_FILENO) == -1) {
                perror("exec_pipeline: dup2");
                return;
            }

            if (close(pipefd[0])) {
                perror("exec_pipeline: close");
                return;
            }

            if (close(pipefd[1])) {
                perror("exec_pipeline: close");
                return;
            }
        }

        /* If p isn't last, make a new pipe for the next directive. */
        if (p->next) {
            if (pipe(pipefd) != 0) {
                perror("exec_pipeline: pipe");
                return;
            }
        }

        if ((p->pid = fork()) == -1) {
            perror("exec_pipeline: fork");
            return;
        }

        if (p->pid == 0) {
            /* If p isn't last, attach stdout to the pipe. */
            if (p->next) {
                if (dup2(pipefd[1], STDOUT_FILENO) == -1) {
                    perror("exec_pipeline: dup2");
                    exit(1);
                }

                if (close(pipefd[0])) {
                    perror("exec_pipeline: close");
                    exit(1);
                }

                if (close(pipefd[1])) {
                    perror("exec_pipeline: close");
                    exit(1);
                }
            }

            if (p->infile) {
                redirect_input(p);
            }

            if (p->outfile) {
                redirect_output(p);
            }

            if (execvp(p->argv[0], p->argv) == -1) {
                perror("exec_pipeline: exec");
                exit(1);
            }
        }

        p = p->next;
    }

    /* Restore stdin. */
    if (dup2(stdin_cpy, STDIN_FILENO) == -1) {
        perror("exec_pipeline: dup2");
        return;
    }

    /* Add directive to jobs list or immediately reap. */
    if (c->bg) {
        shell.jobs[shell.jobp++] = c;
        printf("[%d]", shell.jobp);

        while (c) {
            printf(" %d", c->pid);
            c = c->next;
        }

        printf("\n");
    } else {
        while (c) {
            if (waitpid(c->pid, NULL, 0) == -1) {
                perror("exec_pipeline: waitpid");
                return;
            }

            p = c->next;
            free_command(c);
            c = p;
        }
    }
}

void exec(com_t *c) {
    if (!c) {
        return;
    }

    if (strcmp(c->argv[0], "cd") == 0) {
        exec_cd(c);
    } else if (strcmp(c->argv[0], "exit") == 0) {
        exec_exit(c);
    } else if (strcmp(c->argv[0], "jobs") == 0) {
        exec_jobs(c);
    } else {
        exec_pipeline(c);
    }
}

void reap_jobs(void) {
    com_t *c, *p;
    int i, status;
    for (i = 0; i < shell.jobp; ++i) {
        c = shell.jobs[i];

        errno = 0;
        if ((status = waitpid(shell.jobs[i]->pid, NULL, WNOHANG))) {
            if (status == -1) {
                perror("reap_jobs");
            }

            /* Reap all jobs in pipeline. */
            while (c) {
                printf("[%d] %d done\t", i + 1, c->pid);
                print_command(c);
                printf("\n");

                p = c->next;
                free_command(c);
                c = p;
            }

            /* Remove job from array. */
            int j;
            for (j = i; j < shell.jobp - 1; ++j) {
                shell.jobs[j] = shell.jobs[j + 1];
            }
            --shell.jobp;
        }
    }
}

void print_prompt(void) {
    char *cwd = getcwd(NULL, 0);
    printf("%s smash> ", cwd);
    free(cwd);
}

int main(void) {
    FILE *in = stdin;

    init_shell();

    while (!shell.is_eof) {
        reap_jobs();
        print_prompt();
#ifdef DEBUG
        {
            com_t *c = parse(in);
            command_debug(c);
            exec(c);
        }
#else
        exec(parse(in));
#endif
    }

    return 0;
}
