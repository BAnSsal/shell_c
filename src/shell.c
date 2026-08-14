/*
 * shell.c - a small Linux shell written in C ("mysh")
 * ---------------------------------------------------
 *
 * A beginner-friendly version of what bash does. Every line you type goes
 * through the same four steps:
 *
 *   1. READ      read one line of text                          getline()
 *   2. TOKENIZE  cut the line into words and operators           tokenize()
 *   3. PARSE     group tokens into commands, pick up < > >> &    parse_line()
 *   4. RUN       run a built-in, or fork() + execvp() + waitpid()
 *
 * Supported syntax:
 *
 *   ls -l                        a simple command
 *   echo "hello   world"         quotes keep spaces together
 *   cat f.txt | grep x | wc -l   pipes (any number)
 *   sort < in.txt > out.txt      input / output redirection
 *   echo hi >> log.txt           append instead of overwrite
 *   sleep 5 &                    run in the background
 *   cd  pwd  exit  help  status  built-in commands
 *   # anything after a hash is a comment
 *
 * Build:  gcc -Wall -Wextra -std=c11 -o bin/mysh src/shell.c
 */

/* getline() is POSIX, not plain ISO C. Because we compile with -std=c11 the
 * header hides it unless we ask for the POSIX 2008 interfaces first. This
 * #define has to appear before every #include or it does nothing.          */
#define _POSIX_C_SOURCE 200809L

#include <errno.h>      /* errno, ENOENT, EACCES                            */
#include <fcntl.h>      /* open(), O_RDONLY, O_CREAT, O_TRUNC, O_APPEND     */
#include <signal.h>     /* signal(), SIGINT, SIGQUIT                        */
#include <stdio.h>      /* printf(), fprintf(), perror(), getline()         */
#include <stdlib.h>     /* free(), exit(), getenv(), atoi()                 */
#include <string.h>     /* strcmp(), strlen(), strerror()                   */
#include <sys/stat.h>   /* stat(), S_ISDIR - to detect "is a directory"     */
#include <sys/types.h>  /* pid_t                                            */
#include <sys/wait.h>   /* waitpid(), WIFEXITED, WEXITSTATUS                */
#include <unistd.h>     /* fork(), execvp(), pipe(), dup2(), chdir()        */

/* ------------------------------------------------------------------------ */
/* Limits. Fixed-size arrays keep the code short, and every array is bounds- */
/* checked below, so a long line produces an error message, not a crash.    */
/* ------------------------------------------------------------------------ */
#define MAX_TOKEN 256   /* longest single word                              */
#define MAX_TOKENS 128  /* words + operators in one line                    */
#define MAX_WORDS 64    /* program name + arguments for one command         */
#define MAX_CMDS 16     /* commands in one pipeline: a | b | c ...          */
#define MAX_PATH 4096   /* buffer for getcwd()                              */

/* Exit status of the last command, printed by the built-in `status`.
 * Every shell keeps this; in bash you read it as $?.                        */
static int last_status = 0;

/* ======================================================================== */
/* PART 1 - TOKENIZER: text  ->  list of tokens                             */
/* ======================================================================== */

/*
 * A token is either a word (`ls`, `-l`, `hello world`) or an operator
 * (`|`, `<`, `>`, `>>`, `&`).
 *
 * `is_op` matters: it is how we tell the operator > apart from the word ">"
 * that the user wrote inside quotes as `echo ">"`.
 */
struct token {
    char text[MAX_TOKEN];
    int  is_op;
};

static int is_blank(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

/* Characters that end a word even when no space separates them, which is why
 * `ls|wc -l` and `sort<in.txt` work just like the spaced-out versions. */
static int is_op_char(char c)
{
    return c == '|' || c == '<' || c == '>' || c == '&';
}

/*
 * Split `line` into tokens.
 *
 * Returns the number of tokens, or -1 on a syntax error (an unterminated
 * quote, a word that is too long, or too many tokens). The error message is
 * printed here so the caller only has to stop.
 *
 * This is done with a plain character loop instead of strtok() because strtok()
 * cannot see quotes: it would chop `echo "hello world"` into two arguments.
 */
static int tokenize(const char *line, struct token toks[], int max)
{
    int i = 0;      /* read position in `line`  */
    int n = 0;      /* number of tokens so far  */

    while (line[i] != '\0') {
        while (is_blank(line[i]))
            i++;

        if (line[i] == '\0' || line[i] == '#')
            break;              /* end of line, or start of a comment */

        if (n == max) {
            fprintf(stderr, "mysh: line has too many words (limit is %d)\n",
                    max);
            return -1;
        }

        /* ---- an operator ---- */
        if (is_op_char(line[i])) {
            char op[3] = {line[i], '\0', '\0'};

            if (line[i] == '>' && line[i + 1] == '>') {
                op[1] = '>';    /* ">>" is one operator, not two */
                i++;
            }
            i++;
            strcpy(toks[n].text, op);
            toks[n].is_op = 1;
            n++;
            continue;
        }

        /* ---- a word, possibly containing quotes ---- */
        int len = 0;
        toks[n].is_op = 0;

        while (line[i] != '\0' && !is_blank(line[i]) && !is_op_char(line[i])) {
            char quote = 0;

            if (line[i] == '\'' || line[i] == '"') {
                quote = line[i];
                i++;                        /* step over the opening quote */

                while (line[i] != quote) {
                    if (line[i] == '\0') {
                        fprintf(stderr, "mysh: syntax error: unterminated %c "
                                        "quote\n", quote);
                        return -1;
                    }
                    if (len == MAX_TOKEN - 1) {
                        fprintf(stderr, "mysh: word longer than %d "
                                        "characters\n", MAX_TOKEN - 1);
                        return -1;
                    }
                    toks[n].text[len++] = line[i++];
                }
                i++;                        /* step over the closing quote */
                continue;
            }

            if (line[i] == '\\' && line[i + 1] != '\0')
                i++;            /* \x means "the character x, literally" */

            if (len == MAX_TOKEN - 1) {
                fprintf(stderr, "mysh: word longer than %d characters\n",
                        MAX_TOKEN - 1);
                return -1;
            }
            toks[n].text[len++] = line[i++];
        }

        toks[n].text[len] = '\0';
        n++;
    }
    return n;
}

/* ======================================================================== */
/* PART 2 - PARSER: tokens  ->  commands                                    */
/* ======================================================================== */

/*
 * Everything needed to run ONE command, for example
 *
 *     sort -r < in.txt >> out.txt
 *
 * words   = {"sort", "-r", NULL}  <- exactly the argv execvp() wants
 * infile  = "in.txt"              (NULL when there is no '<')
 * outfile = "out.txt"             (NULL when there is no '>' / '>>')
 * append  = 0 for '>', 1 for '>>'
 */
struct command {
    char *words[MAX_WORDS + 1];
    int   nwords;
    char *infile;
    char *outfile;
    int   append;
};

/*
 * Read the tokens from `start` up to (but not including) `end` into `cmd`.
 * Returns 0 on success, -1 on a syntax error.
 */
static int parse_command(struct token toks[], int start, int end,
                         struct command *cmd)
{
    memset(cmd, 0, sizeof *cmd);        /* no leftovers from the last line */

    for (int i = start; i < end; i++) {
        if (!toks[i].is_op) {
            if (cmd->nwords == MAX_WORDS) {
                fprintf(stderr, "mysh: too many arguments (limit is %d)\n",
                        MAX_WORDS);
                return -1;
            }
            cmd->words[cmd->nwords++] = toks[i].text;
            continue;
        }

        /* An operator: the token after it must be the file name. */
        const char *op = toks[i].text;

        if (i + 1 >= end || toks[i + 1].is_op) {
            fprintf(stderr, "mysh: syntax error: expected a file name after "
                            "'%s'\n", op);
            return -1;
        }
        if (strcmp(op, "<") == 0) {
            cmd->infile = toks[i + 1].text;
        } else if (strcmp(op, ">") == 0 || strcmp(op, ">>") == 0) {
            cmd->outfile = toks[i + 1].text;
            cmd->append  = (strcmp(op, ">>") == 0);
        } else {
            fprintf(stderr, "mysh: syntax error near '%s'\n", op);
            return -1;
        }
        i++;                            /* skip the file name as well */
    }

    if (cmd->nwords == 0) {
        /* "> out.txt" on its own, or an empty pipeline stage. */
        fprintf(stderr, "mysh: syntax error: missing command\n");
        return -1;
    }
    cmd->words[cmd->nwords] = NULL;     /* execvp() needs the NULL at the end */
    return 0;
}

/*
 * Split the token list on '|' and parse each part into cmds[].
 * Also detects a trailing '&' and reports it through *background.
 *
 * Returns the number of commands, or -1 on a syntax error.
 */
static int parse_line(struct token toks[], int ntoks, struct command cmds[],
                      int *background)
{
    int ncmds = 0;
    int start = 0;

    *background = 0;

    /* A '&' is only allowed as the very last token. Anything else, such as
     * `sleep 5 & echo hi`, is rejected instead of silently ignored. */
    for (int i = 0; i < ntoks; i++) {
        if (toks[i].is_op && strcmp(toks[i].text, "&") == 0) {
            if (i != ntoks - 1) {
                fprintf(stderr, "mysh: syntax error near '&' "
                                "(it must be the last thing on the line)\n");
                return -1;
            }
            *background = 1;
            ntoks--;                    /* forget the '&' */
        }
    }

    for (int i = 0; i <= ntoks; i++) {
        int at_pipe = (i < ntoks && toks[i].is_op &&
                       strcmp(toks[i].text, "|") == 0);

        if (i < ntoks && !at_pipe)
            continue;

        /* We are at a '|' or at the end of the line: [start, i) is one stage. */
        if (start == i) {
            /* "| wc -l", "ls | | wc", or a line that is only "|".
             * strtok() used to hide this by skipping empty pieces. */
            fprintf(stderr, "mysh: syntax error near '|'\n");
            return -1;
        }
        if (ncmds == MAX_CMDS) {
            fprintf(stderr, "mysh: too many commands in one pipeline "
                            "(limit is %d)\n", MAX_CMDS);
            return -1;
        }
        if (parse_command(toks, start, i, &cmds[ncmds]) < 0)
            return -1;
        ncmds++;
        start = i + 1;
    }
    return ncmds;
}

/* ======================================================================== */
/* PART 3 - BUILT-IN COMMANDS                                               */
/* ======================================================================== */

/*
 * Why built-ins have to exist: `cd` MUST run in the shell process itself.
 * If we forked a child and called chdir() there, the child would move to the
 * new directory and then exit, leaving the shell exactly where it started.
 */
static void builtin_help(void)
{
    printf("mysh - a small shell for learning how processes work\n\n");
    printf("built-in commands\n");
    printf("  cd [dir]     change directory (no argument means $HOME)\n");
    printf("  pwd          print the working directory\n");
    printf("  status       print the exit status of the last command\n");
    printf("  help         show this text\n");
    printf("  exit [code]  leave the shell\n\n");
    printf("syntax\n");
    printf("  prog args          run a program found in $PATH\n");
    printf("  a | b | c          send the output of a into b, then into c\n");
    printf("  prog < in > out    redirect input and output\n");
    printf("  prog >> out        append to a file\n");
    printf("  prog &             run in the background\n");
}

static int builtin_cd(struct command *cmd)
{
    const char *target;

    if (cmd->nwords > 2) {
        fprintf(stderr, "mysh: cd: too many arguments\n");
        return 1;
    }
    if (cmd->nwords == 1) {
        target = getenv("HOME");
        if (target == NULL) {
            fprintf(stderr, "mysh: cd: HOME is not set\n");
            return 1;
        }
    } else {
        target = cmd->words[1];
    }

    if (chdir(target) != 0) {
        /* strerror(errno) turns the kernel's error number into text, e.g.
         * "mysh: cd: /nope: No such file or directory". */
        fprintf(stderr, "mysh: cd: %s: %s\n", target, strerror(errno));
        return 1;
    }
    return 0;
}

static int builtin_pwd(void)
{
    char buf[MAX_PATH];

    if (getcwd(buf, sizeof buf) == NULL) {
        perror("mysh: pwd");
        return 1;
    }
    printf("%s\n", buf);
    return 0;
}

/*
 * Try to run `cmd` as a built-in.
 * Returns 1 if it was one (its status is stored in last_status), 0 if it is an
 * ordinary program that has to be forked.
 */
static int run_builtin(struct command *cmd, int *should_exit, int *exit_code)
{
    const char *name = cmd->words[0];

    if (strcmp(name, "exit") == 0) {
        *should_exit = 1;
        *exit_code   = (cmd->nwords > 1) ? atoi(cmd->words[1]) : last_status;
        return 1;
    }
    if (strcmp(name, "cd") == 0) {
        last_status = builtin_cd(cmd);
        return 1;
    }
    if (strcmp(name, "pwd") == 0) {
        last_status = builtin_pwd();
        return 1;
    }
    if (strcmp(name, "help") == 0) {
        builtin_help();
        last_status = 0;
        return 1;
    }
    if (strcmp(name, "status") == 0) {
        printf("%d\n", last_status);
        return 1;                       /* leave last_status untouched */
    }
    return 0;
}

/* ======================================================================== */
/* PART 4 - RUNNING PROGRAMS                                                */
/* ======================================================================== */

/*
 * Runs in the CHILD process, just before execvp().
 *
 * open() hands us a fresh file descriptor (3, 4, ...), but the program we are
 * about to exec knows nothing about it: it reads from fd 0 and writes to fd 1.
 * dup2(fd, 0) makes fd 0 refer to the same open file, so the program reads our
 * file without noticing anything happened.
 *
 * Returns 0 on success, -1 if a file could not be opened.
 */
static int apply_redirection(struct command *cmd)
{
    if (cmd->infile != NULL) {
        int fd = open(cmd->infile, O_RDONLY);
        if (fd < 0) {
            fprintf(stderr, "mysh: %s: %s\n", cmd->infile, strerror(errno));
            return -1;
        }
        if (dup2(fd, STDIN_FILENO) < 0) {
            perror("mysh: dup2");
            close(fd);
            return -1;
        }
        close(fd);      /* the copy on fd 0 stays open; this one is spare */
    }

    if (cmd->outfile != NULL) {
        /* O_TRUNC empties an existing file ('>'), O_APPEND writes at the end
         * ('>>'). 0644 = owner can read and write, everyone else can read. */
        int flags = O_WRONLY | O_CREAT | (cmd->append ? O_APPEND : O_TRUNC);
        int fd    = open(cmd->outfile, flags, 0644);
        if (fd < 0) {
            fprintf(stderr, "mysh: %s: %s\n", cmd->outfile, strerror(errno));
            return -1;
        }
        if (dup2(fd, STDOUT_FILENO) < 0) {
            perror("mysh: dup2");
            close(fd);
            return -1;
        }
        close(fd);
    }
    return 0;
}

/*
 * Also runs in the child: replace this process with the requested program.
 * If execvp() ever returns, it failed, so report why and leave.
 *
 * The exit codes are the ones every Unix shell uses:
 *   127 = command not found
 *   126 = found, but could not be executed (a directory, or no +x bit)
 */
static void exec_or_die(struct command *cmd)
{
    struct stat st;

    execvp(cmd->words[0], cmd->words);

    if (errno == ENOENT) {
        fprintf(stderr, "mysh: %s: command not found\n", cmd->words[0]);
        _exit(127);
    }
    if (errno == EACCES && stat(cmd->words[0], &st) == 0 &&
        S_ISDIR(st.st_mode)) {
        fprintf(stderr, "mysh: %s: is a directory\n", cmd->words[0]);
        _exit(126);
    }
    fprintf(stderr, "mysh: %s: %s\n", cmd->words[0], strerror(errno));
    _exit(126);
}

/*
 * Turn the raw value waitpid() gives us into the number a shell reports.
 * A process killed by signal N reports 128 + N, so Ctrl-C (SIGINT is 2) shows
 * up as 130 - exactly what bash prints.
 */
static int status_to_code(int wstatus)
{
    if (WIFEXITED(wstatus))
        return WEXITSTATUS(wstatus);
    if (WIFSIGNALED(wstatus))
        return 128 + WTERMSIG(wstatus);
    return 1;
}

/*
 * Run a pipeline of `ncmds` commands.
 *
 * For `a | b | c` two pipes are needed:
 *
 *   a --write--> pipe1 --read--> b --write--> pipe2 --read--> c
 *
 * The loop keeps one pipe alive at a time: `prev_read` is the read end of the
 * pipe made in the previous round, and becomes the stdin of the current child.
 *
 * Closing descriptors is the part that trips everyone up. A reader only sees
 * end-of-file once EVERY copy of the write end is closed, so the parent has to
 * close its own copies after handing them to the children - otherwise `wc -l`
 * waits forever for input that can never arrive.
 */
static void run_pipeline(struct command cmds[], int ncmds, int background)
{
    pid_t pids[MAX_CMDS];
    int   prev_read = -1;               /* read end of previous pipe, -1 none */
    int   i;

    for (i = 0; i < ncmds; i++) {
        int fd[2] = {-1, -1};

        if (i < ncmds - 1 && pipe(fd) < 0) {
            perror("mysh: pipe");
            break;
        }

        /* Flush before forking: the child inherits a copy of whatever is still
         * sitting in our stdout buffer and would print it a second time. */
        fflush(stdout);

        pid_t pid = fork();
        if (pid < 0) {
            perror("mysh: fork");
            if (fd[0] >= 0) {
                close(fd[0]);
                close(fd[1]);
            }
            break;
        }

        if (pid == 0) {
            /* ------------------------- child ------------------------- */

            /* The shell ignores Ctrl-C (see main), but the programs it starts
             * should react to it normally, so restore the default. */
            signal(SIGINT, SIG_DFL);
            signal(SIGQUIT, SIG_DFL);

            if (prev_read != -1) {              /* read from previous pipe */
                dup2(prev_read, STDIN_FILENO);
                close(prev_read);
            }
            if (i < ncmds - 1) {                /* write into the next pipe */
                close(fd[0]);                   /* we never read this end */
                dup2(fd[1], STDOUT_FILENO);
                close(fd[1]);
            }

            /* A file redirection is applied after the pipe, so it wins:
             * `ls | wc -l > out.txt` puts wc's output in the file. */
            if (apply_redirection(&cmds[i]) < 0)
                _exit(1);

            exec_or_die(&cmds[i]);              /* never returns */
        }

        /* ------------------------- parent ------------------------- */
        pids[i] = pid;

        if (prev_read != -1)
            close(prev_read);           /* the child has its own copy now */
        if (i < ncmds - 1) {
            close(fd[1]);               /* IMPORTANT: or the reader hangs */
            prev_read = fd[0];
        }
    }

    int started = i;                    /* how many children we really forked */

    if (background) {
        /* Do not wait: print the pid like bash does and return to the prompt.
         * reap_background() collects the child later. */
        if (started > 0)
            printf("[background] pid %d\n", (int)pids[started - 1]);
        last_status = 0;
        return;
    }

    /* Wait for every child. The status of a pipeline is the status of its LAST
     * command, which is why bash considers `false | true` a success. */
    for (int k = 0; k < started; k++) {
        int wstatus;

        if (waitpid(pids[k], &wstatus, 0) < 0) {
            perror("mysh: waitpid");
            continue;
        }
        if (k == started - 1)
            last_status = status_to_code(wstatus);
    }
}

/*
 * Background children turn into zombies when they finish: the kernel keeps
 * their exit status around until somebody calls wait(). WNOHANG means "report
 * finished children but do not block if there are none".
 */
static void reap_background(void)
{
    int   wstatus;
    pid_t pid;

    while ((pid = waitpid(-1, &wstatus, WNOHANG)) > 0)
        printf("[done] pid %d exited with status %d\n",
               (int)pid, status_to_code(wstatus));
}

/* ======================================================================== */
/* PART 5 - THE MAIN LOOP                                                   */
/* ======================================================================== */

static void execute_line(const char *line, int *should_exit, int *exit_code)
{
    struct token   toks[MAX_TOKENS];
    struct command cmds[MAX_CMDS];
    int            ntoks, ncmds, background;

    ntoks = tokenize(line, toks, MAX_TOKENS);
    if (ntoks < 0) {
        last_status = 2;                /* 2 = syntax error, same as bash */
        return;
    }
    if (ntoks == 0)
        return;                         /* blank line or a comment */

    ncmds = parse_line(toks, ntoks, cmds, &background);
    if (ncmds < 0) {
        last_status = 2;
        return;
    }

    /* A built-in on its own runs in the shell itself. Inside a pipeline it
     * would need to run in a child, so we keep things simple and only accept
     * built-ins as a single command. */
    if (ncmds == 1 && !background &&
        run_builtin(&cmds[0], should_exit, exit_code))
        return;

    run_pipeline(cmds, ncmds, background);
}

static void print_prompt(void)
{
    char        buf[MAX_PATH];
    const char *dir = getcwd(buf, sizeof buf) ? buf : "?";

    printf("mysh:%s$ ", dir);
    fflush(stdout);         /* the prompt has no '\n', so push it out now */
}

int main(void)
{
    char  *line        = NULL;          /* getline() allocates this for us */
    size_t cap         = 0;
    int    should_exit = 0;
    int    exit_code   = 0;
    int    interactive = isatty(STDIN_FILENO);

    /* When stdout is a terminal the C library flushes at every newline, but
     * when it is a pipe or a file it stores about 4 KB before writing
     * anything. Our children write to the same place directly, so without
     * this our own messages would appear far too late. _IOLBF = line buffered. */
    setvbuf(stdout, NULL, _IOLBF, 0);

    /* Ctrl-C should kill the running command, not the shell. Ignoring SIGINT
     * here is the simplest way to get that; every child puts the default
     * handler back right after fork(). */
    signal(SIGINT, SIG_IGN);
    signal(SIGQUIT, SIG_IGN);

    while (!should_exit) {
        if (interactive) {
            reap_background();
            print_prompt();
        }

        if (getline(&line, &cap, stdin) == -1) {
            /* End of input: Ctrl-D, or the end of a script piped into us. */
            if (interactive)
                printf("exit\n");
            break;
        }

        execute_line(line, &should_exit, &exit_code);
    }

    free(line);
    return should_exit ? exit_code : last_status;
}
