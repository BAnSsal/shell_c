# Linux Shell & CPU Scheduling — Version 1

A small Operating Systems project in C, written to be **read and understood
line by line**, not just executed.

It contains three programs:

| program     | what it is                                                                  | source                                 |
| ----------- | --------------------------------------------------------------------------- | -------------------------------------- |
| `bin/mysh`  | a tiny Linux shell: runs commands, pipes, redirection, background jobs      | `src/shell.c`                          |
| `bin/fcfs`  | a First Come First Served CPU scheduling simulation                         | `src/fcfs.c`                           |
| `bin/mlfq`  | a Multi-Level Feedback Queue CPU scheduling simulation                       | `src/mlfq.c`                           |

Everything is plain C11 with the standard POSIX library. No external
dependencies, no frameworks, about 1,500 lines of code in total, and roughly
half of those lines are comments explaining *why* the code is written that way.

---

## Table of contents

1. [Quick start](#1-quick-start)
2. [Project layout](#2-project-layout)
3. [The operating-system ideas you need first](#3-the-operating-system-ideas-you-need-first)
4. [Part 1 — the shell, line by line](#4-part-1--the-shell-line-by-line)
5. [Part 2 — FCFS, line by line](#5-part-2--fcfs-line-by-line)
6. [Part 3 — MLFQ, line by line](#6-part-3--mlfq-line-by-line)
7. [FCFS vs MLFQ — what the numbers say](#7-fcfs-vs-mlfq--what-the-numbers-say)
8. [Development log: every command I ran, every problem I hit](#8-development-log-every-command-i-ran-every-problem-i-hit)
9. [Testing](#9-testing)
10. [Questions you may be asked, with answers](#10-questions-you-may-be-asked-with-answers)
11. [Limitations and ideas for version 2](#11-limitations-and-ideas-for-version-2)

---

## 1. Quick start

```bash
# build all three programs into bin/
make

# 1) the shell
./bin/mysh
mysh:/home/you/shell_c$ echo hello world
mysh:/home/you/shell_c$ ls | wc -l
mysh:/home/you/shell_c$ sort < tests/workload1.txt | head -3
mysh:/home/you/shell_c$ echo saved > /tmp/out.txt
mysh:/home/you/shell_c$ sleep 3 &
mysh:/home/you/shell_c$ help
mysh:/home/you/shell_c$ exit

# 2) FCFS on the sample workload
./bin/fcfs tests/workload1.txt

# 3) MLFQ on the same workload, so the two can be compared
./bin/mlfq tests/workload1.txt

# 4) run the automated tests (46 checks)
make test
```

Other useful targets:

```bash
make run-shell     # build if needed, then start the shell
make run-fcfs      # FCFS on tests/workload1.txt
make run-mlfq      # MLFQ on tests/workload1.txt
make clean         # delete bin/
```

Both schedulers accept a workload file, `-` for standard input, or nothing at
all (in which case a built-in example is used):

```bash
./bin/fcfs                       # built-in example workload
./bin/mlfq tests/workload2.txt   # from a file
printf 'P1 0 5\nP2 1 3\n' | ./bin/fcfs -   # from standard input
./bin/mlfq --help                # explains the file format
```

---

## 2. Project layout

```
.
├── Makefile                  how to build everything
├── README.md                 this document
├── src
│   ├── shell.c               the whole shell (one file, 5 clearly marked parts)
│   ├── scheduler.h           the types and helpers the two schedulers share
│   ├── scheduler_common.c    workload loading, Gantt chart, timeline, metrics
│   ├── fcfs.c                the FCFS algorithm + its main()
│   └── mlfq.c                the MLFQ algorithm + its main()
└── tests
    ├── run_tests.sh          46 automated checks
    ├── workload1.txt         the classic "convoy effect" workload
    ├── workload2.txt         one very long process + two late arrivals
    └── workload3.txt         a workload that leaves the CPU idle
```

Why `scheduler_common.c` exists: FCFS and MLFQ differ *only* in how they choose
the next process. Reading the workload and printing the results is identical, so
that code lives in one place. The result is that `fcfs.c` and `mlfq.c` contain
almost nothing but the algorithm itself, which makes them easy to compare.

---

## 3. The operating-system ideas you need first

Six ideas explain almost every line of `src/shell.c`.

### 3.1 A process is a running program

Each has a number, its **pid**. `getpid()` returns yours.

### 3.2 `fork()` duplicates the current process

After `fork()` there are two nearly identical processes, and the return value is
the only way to tell which one you are:

```c
pid_t pid = fork();
if (pid == 0) {
    /* I am the child. */
} else if (pid > 0) {
    /* I am the parent, and pid is my child's pid. */
} else {
    /* fork failed - no child was created. */
}
```

The child gets a **copy** of the parent's memory, so changing a variable in the
child does not change it in the parent. This is exactly why `cd` cannot be an
ordinary program (see §4.5).

### 3.3 `exec` replaces the program inside a process

`execvp("ls", argv)` throws away the current program and loads `ls` in its
place, keeping the same pid. **If `execvp` returns, it failed** — there is no
"success" return value, because on success the code that called it no longer
exists.

So "run a command" is always the same two-step dance: `fork()` to get a spare
process, then `exec` inside that spare process.

### 3.4 `waitpid()` collects the child's exit status

A finished child stays in the process table as a **zombie** until its parent
asks for its status. The status is not a plain integer; it is packed, and you
unpack it with macros:

```c
int wstatus;
waitpid(pid, &wstatus, 0);
if (WIFEXITED(wstatus))        /* ended normally  */ code = WEXITSTATUS(wstatus);
else if (WIFSIGNALED(wstatus)) /* killed by signal */ code = 128 + WTERMSIG(wstatus);
```

That `128 + signal` convention is why Ctrl-C shows up as status 130
(128 + SIGINT, and SIGINT is 2).

### 3.5 File descriptors are small integers

Every open file in a process is a number. Three of them exist from the start:

| fd | name           | normally connected to |
| -- | -------------- | --------------------- |
| 0  | standard input  | the keyboard         |
| 1  | standard output | the screen           |
| 2  | standard error  | the screen           |

Programs are written to always use 0, 1 and 2. Redirection works by quietly
changing *what* those numbers point at — the program never notices:

```c
int fd = open("out.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644); /* e.g. fd == 3 */
dup2(fd, STDOUT_FILENO);   /* now fd 1 also refers to out.txt */
close(fd);                 /* the spare copy is no longer needed */
```

### 3.6 A pipe is a one-way buffer with two ends

`pipe(fd)` fills a two-element array: `fd[0]` is the **read** end and `fd[1]`
is the **write** end. Whatever is written to `fd[1]` can be read from `fd[0]`.

The rule that traps everyone: **a reader only sees end-of-file when every copy
of the write end is closed.** `fork()` duplicates descriptors, so after forking
there are two copies of `fd[1]` — the child's and the parent's. If the parent
forgets to close its copy, the reader waits forever. §8.6 shows that deadlock
actually happening.

---

## 4. Part 1 — the shell, line by line

`src/shell.c` is deliberately one file, split into five labelled parts.
Every line the user types travels through them in order:

```
   "cat f.txt | wc -l > out.txt"
              |
        PART 1 tokenize()      ->  [cat] [f.txt] [|] [wc] [-l] [>] [out.txt]
              |
        PART 2 parse_line()    ->  cmd0: words={cat,f.txt}
              |                    cmd1: words={wc,-l}, outfile="out.txt"
              |
        PART 3 run_builtin()   ->  is it cd/pwd/exit/help/status? no
              |
        PART 4 run_pipeline()  ->  pipe(), fork(), dup2(), execvp(), waitpid()
              |
        PART 5 main() loop     ->  print the prompt again
```

### 4.1 `main()` — the read-eval-print loop

```c
int    interactive = isatty(STDIN_FILENO);

setvbuf(stdout, NULL, _IOLBF, 0);
signal(SIGINT,  SIG_IGN);
signal(SIGQUIT, SIG_IGN);

while (!should_exit) {
    if (interactive) {
        reap_background();
        print_prompt();
    }
    if (getline(&line, &cap, stdin) == -1) {   /* Ctrl-D or end of script */
        if (interactive)
            printf("exit\n");
        break;
    }
    execute_line(line, &should_exit, &exit_code);
}
free(line);
return should_exit ? exit_code : last_status;
```

Line by line:

- `isatty(STDIN_FILENO)` asks "is my input a real terminal?". When you pipe a
  script into the shell (`echo 'ls' | ./bin/mysh`) the answer is no, so the
  prompt is skipped. Without this the test output would be full of prompts.
- `setvbuf(..., _IOLBF, 0)` makes our own output flush at every newline. §8.3
  explains the bug that forced this line to exist.
- `signal(SIGINT, SIG_IGN)` makes the **shell** ignore Ctrl-C. Children put the
  default behaviour back (§4.8), so Ctrl-C kills the running command and leaves
  your shell alive — exactly what a real shell does.
- `getline(&line, &cap, stdin)` reads one line and grows the buffer itself, so
  there is no fixed line-length limit and no `gets()`-style overflow. It
  returns `-1` at end of input, which is how Ctrl-D exits the shell.
- The final `return` makes the shell's own exit status meaningful:
  `exit 7` really does return 7 to whoever started `mysh`.

### 4.2 PART 1 — `tokenize()`: text becomes tokens

A token is either a **word** (`ls`, `-l`, `hello world`) or an **operator**
(`|`, `<`, `>`, `>>`, `&`):

```c
struct token {
    char text[MAX_TOKEN];
    int  is_op;
};
```

`is_op` exists so the shell can tell the operator `>` apart from the *word*
`">"` that the user typed inside quotes. Without that flag,
`echo ">"` would be parsed as a redirection with no file name.

The function is a single loop over the characters of the line:

```c
while (line[i] != '\0') {
    while (is_blank(line[i]))          /* 1. skip spaces and tabs        */
        i++;
    if (line[i] == '\0' || line[i] == '#')
        break;                         /* 2. end of line, or a comment   */

    if (is_op_char(line[i])) {         /* 3. an operator                 */
        char op[3] = {line[i], '\0', '\0'};
        if (line[i] == '>' && line[i + 1] == '>') {
            op[1] = '>';               /*    ">>" is ONE operator        */
            i++;
        }
        ...
        continue;
    }
    ...                                /* 4. otherwise it is a word      */
}
```

- Step 3 is why `ls|wc -l` works with no spaces: `|` ends the previous word by
  itself, because `is_op_char()` is checked in the word loop as well.
- The `>>` check has to come before the `>` case, or `>>` would be read as two
  separate `>` operators and the append would silently become an overwrite.

Inside a word, quotes are handled like this:

```c
if (line[i] == '\'' || line[i] == '"') {
    quote = line[i];
    i++;                                    /* step over the opening quote  */
    while (line[i] != quote) {
        if (line[i] == '\0') {
            fprintf(stderr, "mysh: syntax error: unterminated %c quote\n", quote);
            return -1;
        }
        if (len == MAX_TOKEN - 1) { ...too long... }
        toks[n].text[len++] = line[i++];    /* copy the character as-is     */
    }
    i++;                                    /* step over the closing quote  */
    continue;
}
```

- The characters between the quotes are copied **without interpretation**, so
  `echo "hello   world"` keeps all three spaces and stays one argument.
- Reaching `'\0'` before the closing quote is a syntax error, not a crash.
- `len == MAX_TOKEN - 1` is the bounds check that keeps a very long word from
  writing past the end of `text[]`. Leaving that check out is the classic buffer
  overflow.
- The loop continues after the closing quote, which is what makes
  `--name="two words"` a single token: unquoted text and quoted text can be
  glued together inside one word.

A backslash escapes the next character:

```c
if (line[i] == '\\' && line[i + 1] != '\0')
    i++;            /* \x means "the character x, literally" */
```

so `echo hello\ world` is one argument too.

### 4.3 PART 2 — `parse_command()`: tokens become a command

```c
struct command {
    char *words[MAX_WORDS + 1];   /* {"sort", "-r", NULL} - argv for execvp */
    int   nwords;
    char *infile;                 /* "in.txt"  or NULL                     */
    char *outfile;                /* "out.txt" or NULL                     */
    int   append;                 /* 0 for '>', 1 for '>>'                 */
};
```

`words` holds pointers **into** the token array, so nothing is copied and
nothing needs freeing. `words[nwords] = NULL` at the end matters a lot:
`execvp()` finds the end of the argument list by looking for that `NULL`.

The parser walks the tokens of one pipeline stage:

```c
for (int i = start; i < end; i++) {
    if (!toks[i].is_op) {                   /* an ordinary word */
        if (cmd->nwords == MAX_WORDS) { ...error... }
        cmd->words[cmd->nwords++] = toks[i].text;
        continue;
    }
    const char *op = toks[i].text;          /* a redirection */
    if (i + 1 >= end || toks[i + 1].is_op) {
        fprintf(stderr, "mysh: syntax error: expected a file name after '%s'\n", op);
        return -1;
    }
    if (strcmp(op, "<") == 0) {
        cmd->infile = toks[i + 1].text;
    } else if (strcmp(op, ">") == 0 || strcmp(op, ">>") == 0) {
        cmd->outfile = toks[i + 1].text;
        cmd->append  = (strcmp(op, ">>") == 0);
    }
    i++;                                    /* skip the file name too */
}
```

- The operator and its file name are consumed **together**: `sort < in.txt`
  must run `sort` with no arguments, not `sort` with the arguments `<` and
  `in.txt`.
- `i + 1 >= end || toks[i + 1].is_op` catches both `echo x >` (nothing follows)
  and `echo x > | wc` (an operator follows).
- After the loop, `nwords == 0` means the user wrote redirections with no
  program, such as `> out.txt`, which is reported as "missing command".

`parse_line()` then splits the token list on `|`:

```c
for (int i = 0; i <= ntoks; i++) {
    int at_pipe = (i < ntoks && toks[i].is_op && strcmp(toks[i].text, "|") == 0);
    if (i < ntoks && !at_pipe)
        continue;
    if (start == i) {                      /* "| wc", "a | | b", "ls |" */
        fprintf(stderr, "mysh: syntax error near '|'\n");
        return -1;
    }
    if (parse_command(toks, start, i, &cmds[ncmds]) < 0)
        return -1;
    ncmds++;
    start = i + 1;
}
```

The loop condition is `i <= ntoks`, one past the end, so the final stage (which
is not followed by a `|`) is parsed by the same code as all the others.
`start == i` means the stage is empty — that check is the fix for a real bug
described in §8.5.

`&` is handled separately, and only in the last position:

```c
if (toks[i].is_op && strcmp(toks[i].text, "&") == 0) {
    if (i != ntoks - 1) {
        fprintf(stderr, "mysh: syntax error near '&' (it must be the last thing on the line)\n");
        return -1;
    }
    *background = 1;
    ntoks--;                    /* forget the '&' */
}
```

Rejecting `sleep 1 & echo hi` is better than pretending to support it: real
bash would run two separate commands, and this shell does not, so it says so
instead of quietly doing the wrong thing.

### 4.4 PART 3 — the built-in commands

```
cd [dir]      change directory ($HOME when no argument)
pwd           print the working directory
status        print the exit status of the last command   (bash calls this $?)
help          list everything the shell understands
exit [code]   leave the shell, optionally with a status
```

### 4.5 Why built-ins must exist

This is the single most important idea in the shell. `cd` **cannot** be an
external program:

1. `fork()` gives the child a *copy* of the parent's state, including its
   current directory.
2. If the child called `chdir("/tmp")`, only the child would move.
3. The child then exits, and the shell is still where it was.

So `cd` has to run inside the shell process itself, and that is what a built-in
is: a command the shell implements instead of executing. `pwd`, `status` and
`exit` are built-ins for the same reason — they read or change the shell's own
state.

```c
if (chdir(target) != 0) {
    fprintf(stderr, "mysh: cd: %s: %s\n", target, strerror(errno));
    return 1;
}
```

`strerror(errno)` turns the kernel's error number into readable text, so a
typo produces `mysh: cd: /tmpp: No such file or directory` instead of a silent
failure.

### 4.6 PART 4 — `apply_redirection()`: `<`, `>` and `>>`

Runs **in the child**, just before `exec`:

```c
if (cmd->infile != NULL) {
    int fd = open(cmd->infile, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "mysh: %s: %s\n", cmd->infile, strerror(errno));
        return -1;
    }
    if (dup2(fd, STDIN_FILENO) < 0) { perror("mysh: dup2"); close(fd); return -1; }
    close(fd);
}
```

- `open()` returns a fresh descriptor such as 3. The program about to be
  `exec`'d knows nothing about descriptor 3 — it reads from 0. `dup2(fd, 0)`
  makes descriptor 0 point at the same open file, so the program reads the file
  while believing it is reading the keyboard.
- `close(fd)` afterwards closes only the *spare* copy. Descriptor 0 keeps the
  file open. Forgetting this leaks a descriptor into every command you run.
- The output side is the same idea with different flags:

```c
int flags = O_WRONLY | O_CREAT | (cmd->append ? O_APPEND : O_TRUNC);
int fd    = open(cmd->outfile, flags, 0644);
```

| flag       | meaning                                              |
| ---------- | ---------------------------------------------------- |
| `O_WRONLY` | we only intend to write                              |
| `O_CREAT`  | create the file if it does not exist                 |
| `O_TRUNC`  | `>` — empty an existing file first                   |
| `O_APPEND` | `>>` — every write goes to the end of the file       |
| `0644`     | permissions for a newly created file: `rw-r--r--`    |

The single difference between `>` and `>>` is `O_TRUNC` versus `O_APPEND`.
This is a favourite exam question.

### 4.7 PART 4 — `exec_or_die()`: the error codes a shell must return

```c
execvp(cmd->words[0], cmd->words);

if (errno == ENOENT) {
    fprintf(stderr, "mysh: %s: command not found\n", cmd->words[0]);
    _exit(127);
}
if (errno == EACCES && stat(cmd->words[0], &st) == 0 && S_ISDIR(st.st_mode)) {
    fprintf(stderr, "mysh: %s: is a directory\n", cmd->words[0]);
    _exit(126);
}
fprintf(stderr, "mysh: %s: %s\n", cmd->words[0], strerror(errno));
_exit(126);
```

- Any line after `execvp` only runs if `execvp` **failed**.
- `execvp` (the `p` means "search `PATH`") is why `ls` works without typing
  `/usr/bin/ls`.
- `127` = command not found, `126` = found but not runnable. These are the same
  numbers bash uses, which is why `nosuchcommand; status` prints `127` here just
  as `nosuchcommand; echo $?` does in bash.
- `_exit()` rather than `exit()`: `exit()` would flush the stdio buffers that
  this process inherited from the shell, printing the parent's pending output a
  second time. `_exit()` ends the process immediately without touching them.

### 4.8 PART 4 — `run_pipeline()`: the heart of the shell

For `a | b | c` two pipes are needed:

```
   a ==write==> [ pipe1 ] ==read==> b ==write==> [ pipe2 ] ==read==> c
```

The loop keeps exactly one pipe alive at a time. `prev_read` is the read end
left over from the previous round, and becomes the stdin of the current child:

```c
for (i = 0; i < ncmds; i++) {
    int fd[2] = {-1, -1};

    if (i < ncmds - 1 && pipe(fd) < 0) { perror("mysh: pipe"); break; }

    fflush(stdout);                     /* see §8.3 */

    pid_t pid = fork();
    if (pid < 0) { perror("mysh: fork"); ...close fds...; break; }

    if (pid == 0) {
        /* ---- child ---- */
        signal(SIGINT,  SIG_DFL);       /* Ctrl-C should kill me, not the shell */
        signal(SIGQUIT, SIG_DFL);

        if (prev_read != -1) {          /* not the first stage: read the pipe */
            dup2(prev_read, STDIN_FILENO);
            close(prev_read);
        }
        if (i < ncmds - 1) {            /* not the last stage: write the pipe */
            close(fd[0]);               /* this child never reads it */
            dup2(fd[1], STDOUT_FILENO);
            close(fd[1]);
        }

        if (apply_redirection(&cmds[i]) < 0)
            _exit(1);
        exec_or_die(&cmds[i]);          /* never returns */
    }

    /* ---- parent ---- */
    pids[i] = pid;
    if (prev_read != -1)
        close(prev_read);               /* the child has its own copy now */
    if (i < ncmds - 1) {
        close(fd[1]);                   /* IMPORTANT: or the reader hangs */
        prev_read = fd[0];
    }
}
```

The important details, in order of how easy they are to get wrong:

1. **`close(fd[1])` in the parent.** The child inherited a copy of the write
   end, so two copies exist. `wc -l` keeps waiting for more input until *all*
   copies are closed. Leave this line out and `echo hi | wc -l` hangs forever —
   §8.6 shows the experiment.
2. **`close(prev_read)` in the parent** after each fork, for the same reason on
   the read side, and to avoid running out of descriptors on long pipelines.
3. **The child closes the ends it does not use** (`close(fd[0])`), so it does not
   hold a pipe open for a sibling.
4. **Redirection is applied after the pipe wiring**, so a file wins over the
   pipe. That is why `ls | wc -l > out.txt` sends `wc`'s output to the file.
5. **The signal handlers are restored in the child, before `exec`.** Signal
   *dispositions* survive `exec`, so a command started by a shell that ignores
   SIGINT would ignore Ctrl-C too — an un-killable `sleep 100`.

Then the parent waits:

```c
for (int k = 0; k < started; k++) {
    int wstatus;
    if (waitpid(pids[k], &wstatus, 0) < 0) { perror("mysh: waitpid"); continue; }
    if (k == started - 1)
        last_status = status_to_code(wstatus);
}
```

Every child is waited for, so no zombies are left behind, but only the **last**
one sets the status. That is standard shell behaviour:

```
mysh$ false | true
mysh$ status
0
```

`started` (not `ncmds`) is used because a failed `pipe()` or `fork()` may have
stopped the loop early; waiting for children that were never created would
block forever.

### 4.9 Background jobs and zombies

```c
if (background) {
    if (started > 0)
        printf("[background] pid %d\n", (int)pids[started - 1]);
    last_status = 0;
    return;                     /* no waitpid: back to the prompt at once */
}
```

The child is left running. When it finishes, nobody has collected its status
yet, so it becomes a **zombie**: a dead process that still occupies a slot in
the process table. `reap_background()`, called before every prompt, clears them
out:

```c
while ((pid = waitpid(-1, &wstatus, WNOHANG)) > 0)
    printf("[done] pid %d exited with status %d\n", (int)pid, status_to_code(wstatus));
```

- `-1` means "any child of mine".
- `WNOHANG` means "return immediately if nothing has finished" — without it the
  shell would freeze at the prompt waiting for a background job to end.
- The `while` loop matters: several jobs may have finished since the last prompt.

Real session:

```
mysh:/tmp$ sleep 30
^Cmysh:/tmp$ status
130                                     <- 128 + SIGINT(2): Ctrl-C killed sleep,
mysh:/tmp$ sleep 1 &                       and the shell survived
[background] pid 3474
mysh:/tmp$ echo still-alive
still-alive
mysh:/tmp$ echo second-prompt-after-bg
second-prompt-after-bg
[done] pid 3474 exited with status 0    <- reaped at the next prompt
```

### 4.10 A complete worked trace

Command: `sort < in.txt | uniq -c > counts.txt`

1. `tokenize()` produces 9 tokens:
   `[sort] [<] [in.txt] [|] [uniq] [-c] [>] [counts.txt]`
   (the operators carry `is_op = 1`).
2. `parse_line()` finds `|` at index 3 and makes two stages:
   - stage 0: `words = {sort}`, `infile = "in.txt"`
   - stage 1: `words = {uniq, -c}`, `outfile = "counts.txt"`, `append = 0`
3. Two stages, so no built-in lookup; `run_pipeline(cmds, 2, 0)` is called.
4. `i = 0`: `pipe(fd)` returns, say, `fd = {3, 4}`. `fork()` gives child A.
   - Child A: `i < ncmds-1`, so `close(3)`, `dup2(4, 1)`, `close(4)` — its
     stdout is the pipe. `apply_redirection()` opens `in.txt` and `dup2`s it
     onto fd 0. `execvp("sort", ...)` replaces the process.
   - Parent: `close(4)` (critical), `prev_read = 3`.
5. `i = 1`: last stage, so no new pipe. `fork()` gives child B.
   - Child B: `dup2(3, 0)`, `close(3)` — its stdin is the pipe.
     `apply_redirection()` opens `counts.txt` with `O_CREAT | O_TRUNC` and
     `dup2`s it onto fd 1. `execvp("uniq", ...)`.
   - Parent: `close(3)`.
6. The parent now holds **no** pipe descriptors, so when `sort` finishes and
   exits, `uniq` sees end-of-file, writes its output and exits too.
7. `waitpid()` collects both; `uniq`'s exit code becomes `last_status`.

### 4.11 What the shell supports, and what it does not

Supported: `PATH` lookup, arguments, single and double quotes, backslash
escapes, `#` comments, `|` (up to 16 stages), `<`, `>`, `>>`, trailing `&`, the
five built-ins, Ctrl-C and Ctrl-D handling, zombie reaping, and bash-compatible
exit codes (0, 1, 2, 126, 127, 128+signal).

Not supported in version 1, on purpose: variables and `$?` expansion (use the
`status` built-in), globbing (`*.c`), `&&`, `||`, `;`, `2>`, `2>&1`, here-docs,
command substitution, `fg`/`bg`/`jobs`, and history with the arrow keys. §11
lists these as version 2 work.

---

## 5. Part 2 — FCFS, line by line

### 5.1 The idea

**First Come First Served**: whoever asks first is served first, and nobody is
ever interrupted. It is the queue at a shop counter, and it is
**non-preemptive** — once a process gets the CPU it keeps it until it is done.

### 5.2 Input format

`tests/workload1.txt`:

```
# name  arrival  burst
P1      0        8
P2      1        4
P3      2        9
P4      3        2
```

- **arrival** — the time unit at which the process shows up.
- **burst** — how many time units of CPU it needs in total.

Blank lines and `#` comments are ignored. `load_workload()` in
`scheduler_common.c` validates every line and refuses `arrival < 0` or
`burst <= 0`, so a typo produces a clear message and exit status 1 instead of a
nonsense simulation.

### 5.3 The algorithm

Sorting first, with a selection sort that is easy to read:

```c
for (int i = 0; i < n - 1; i++) {
    int min = i;
    for (int j = i + 1; j < n; j++)
        if (procs[j].arrival < procs[min].arrival)
            min = j;
    if (min != i) { struct process tmp = procs[i]; procs[i] = procs[min]; procs[min] = tmp; }
}
```

The comparison is strictly `<`, so processes that arrive at the same time keep
the order they had in the file. For a tie, "first come" can only mean "first
listed".

Then the simulation itself — this is the whole scheduler:

```c
for (int i = 0; i < n; i++) {
    while (time < procs[i].arrival) {       /* 1. nothing has arrived yet   */
        add_tick(segs, &nsegs, -1, time);   /*    record an idle time unit  */
        time++;
    }
    procs[i].start_time = time;             /* 2. it gets the CPU now       */
    for (int k = 0; k < procs[i].burst; k++) {
        add_tick(segs, &nsegs, i, time);    /* 3. run it to completion      */
        time++;
        procs[i].remaining--;
    }
    procs[i].finish_time = time;            /* 4. next process please       */
}
```

- `time` is the clock, and it only moves forward.
- Step 1 handles a gap: if the next process arrives at 8 and the clock is at 2,
  the CPU is idle for six time units. Recording those idle units (as process
  `-1`) is what makes the Gantt chart show the gap instead of hiding it.
- Step 3 loops one time unit at a time. FCFS does not need that — it could just
  do `time += burst` — but writing it this way makes the FCFS loop and the MLFQ
  loop look alike, and in MLFQ the tick-by-tick structure is essential.
- `start_time` is recorded once, at the moment the process first gets the CPU;
  it is what response time is computed from.

### 5.4 How the metrics are computed

In `print_summary()`:

```c
int turnaround = procs[i].finish_time - procs[i].arrival;
int waiting    = turnaround - procs[i].burst;
int response   = procs[i].start_time - procs[i].arrival;
```

| metric         | meaning                                                        |
| -------------- | -------------------------------------------------------------- |
| **turnaround** | total time from arriving to finishing                          |
| **waiting**    | the part of that time spent *not* running                      |
| **response**   | how long you wait before you see the first sign of life        |

`waiting = turnaround - burst` works because every time unit between arrival and
finish is either CPU time (exactly `burst` of them) or waiting.

Also printed:

- **CPU utilisation** = busy time ÷ total time. Below 100% only when the CPU was
  idle (try `tests/workload3.txt`).
- **throughput** = processes ÷ total time.
- **context switches** = how often the CPU moved from one process to another.
  FCFS needs the bare minimum: one per process.

### 5.5 Actual output

```
$ ./bin/fcfs tests/workload1.txt
Gantt chart
  +----+----+----+----+
  | P1 | P2 | P3 | P4 |
  +----+----+----+----+
  0    8    12   21   23

Per-process timeline   ('#' running, '.' waiting for the CPU)
  P1           ########                 finished at 8
  P2            .......####             finished at 12
  P3             ..........#########    finished at 21
  P4              ..................##  finished at 23

Results (FCFS)
  process       arrival    burst    start   finish  turnaround  waiting  response
  P1                  0        8        0        8           8        0         0
  P2                  1        4        8       12          11        7         7
  P3                  2        9       12       21          19       10        10
  P4                  3        2       21       23          20       18        18

  average turnaround time : 14.50
  average waiting time    : 8.75
  average response time   : 8.75
  total time              : 23 (from 0 to 23)
  CPU utilisation         : 100.0% (23 busy of 23)
  throughput              : 0.174 processes per time unit
  context switches        : 3
```

Read the timeline row for P4: it needs 2 time units of CPU but waits 18 for
them. That is the **convoy effect** — a long process at the front holds up
everything behind it, exactly like one slow trolley at the checkout. It is also
the entire motivation for MLFQ.

Note that under FCFS, average waiting time and average response time are
identical (8.75). They have to be: a process is never interrupted, so the moment
it first runs is the moment it stops waiting.

---

## 6. Part 3 — MLFQ, line by line

### 6.1 The idea

A **Multi-Level Feedback Queue** does not know in advance which process is short
and which is long, so it *learns from behaviour*: a process that keeps using its
whole time slice is probably a long job and gets pushed to a lower priority,
while a process that finishes quickly stays near the top.

This project uses three queues:

```
Q0  (highest priority)  time slice 2   <- every new process starts here
Q1                      time slice 4
Q2  (lowest priority)   time slice 8   <- round robin at the bottom
priority boost: every 15 time units, everything goes back to Q0
```

Lower priority gets a *longer* slice, because a long job then wastes less time
being switched in and out.

### 6.2 The five rules, and the code that implements each

**R1 — a new process enters the highest queue.**

```c
for (int i = 0; i < n; i++) {
    if (procs[i].arrival == time) {
        procs[i].queue = 0;
        q_push(&queues[0], i);
        printf("  t=%-3d %s arrives, joins Q0\n", time, procs[i].name);
    }
}
```

**R5 — every `BOOST_INTERVAL` time units, move everything back to Q0.**

```c
if (BOOST_INTERVAL > 0 && time > 0 && time % BOOST_INTERVAL == 0) {
    for (int q = 1; q < NQUEUES; q++)
        while (!q_empty(&queues[q])) {
            int i = q_pop(&queues[q]);
            procs[i].queue = 0;
            q_push(&queues[0], i);
        }
    if (running >= 0 && procs[running].queue != 0) {
        procs[running].queue = 0;
        used = 0;                       /* it gets a fresh Q0 slice */
    }
}
```

Without R5 a long job demoted to Q2 could **starve** forever while short jobs
keep arriving. The boost is the safety net that guarantees everyone eventually
runs again. The currently running process has already been popped out of its
queue, so it has to be boosted separately — that is what the second `if` is for.

**R4 — a process in a higher queue preempts the running one.**

```c
if (running >= 0) {
    for (int q = 0; q < procs[running].queue; q++) {
        if (!q_empty(&queues[q])) {
            q_push(&queues[procs[running].queue], running);
            running = -1;
            used    = 0;
            break;
        }
    }
}
```

The loop only looks at queues **above** the running process (`q < queue`), so an
arrival at the *same* priority waits its turn instead of interrupting. The
preempted process goes to the **back** of its own queue.

**R2 — always run something from the highest non-empty queue.**

```c
if (running < 0) {
    for (int q = 0; q < NQUEUES; q++)
        if (!q_empty(&queues[q])) {
            running = q_pop(&queues[q]);
            used    = 0;
            break;
        }
}
if (running < 0) {                      /* every queue is empty */
    add_tick(segs, &nsegs, -1, time);   /* the CPU is idle this time unit */
    time++;
    continue;
}
```

Then exactly one time unit is consumed — never more:

```c
if (procs[running].start_time < 0)
    procs[running].start_time = time;   /* its very first turn on the CPU */

add_tick(segs, &nsegs, running, time);
procs[running].remaining--;
used++;
time++;
```

Stepping one tick at a time is what makes R4 possible: a process that arrives in
the middle of somebody else's slice is noticed immediately.

**R3 — using up a whole slice costs you a level.**

```c
if (used == quantum[procs[running].queue]) {
    int old    = procs[running].queue;
    int next_q = (old + 1 < NQUEUES) ? old + 1 : old;
    procs[running].queue = next_q;
    q_push(&queues[next_q], running);
    running = -1;
    used    = 0;
}
```

In the lowest queue `next_q == old`, so the process simply goes to the back of
Q2 — that is plain round robin among the long-running jobs.

Completion is checked before demotion, so a process that finishes exactly at the
end of its slice is not pointlessly demoted first:

```c
if (procs[running].remaining == 0) {
    procs[running].finish_time = time;
    completed++;
    running = -1;
    used    = 0;
    continue;
}
```

### 6.3 The queue data structure

```c
struct queue {
    int items[MAX_PROCS];
    int head;
    int count;
};

static void q_push(struct queue *q, int value)
{
    q->items[(q->head + q->count) % MAX_PROCS] = value;
    q->count++;
}

static int q_pop(struct queue *q)
{
    int value = q->items[q->head];
    q->head = (q->head + 1) % MAX_PROCS;
    q->count--;
    return value;
}
```

A **circular buffer**: `head` is where the next pop comes from, and the free
slot for a push is `(head + count) % MAX_PROCS`. The `%` is what makes the array
wrap around, so pushing and popping thousands of times never runs off the end.
A process is only ever in one queue at a time, so `MAX_PROCS` slots always
suffice.

### 6.4 Actual output, explained event by event

```
$ ./bin/mlfq tests/workload1.txt
Scheduling events
  t=0   P1 arrives, joins Q0
  t=1   P2 arrives, joins Q0
  t=2   P1 used its 2-unit slice in Q0 -> demoted to Q1
  t=2   P3 arrives, joins Q0
  t=3   P4 arrives, joins Q0
  t=4   P2 used its 2-unit slice in Q0 -> demoted to Q1
  t=6   P3 used its 2-unit slice in Q0 -> demoted to Q1
  t=8   P4 finished (was in Q0)
  t=12  P1 used its 4-unit slice in Q1 -> demoted to Q2
  t=14  P2 finished (was in Q1)
  t=15  priority boost: 2 process(es) moved back to Q0
  t=17  P3 used its 2-unit slice in Q0 -> demoted to Q1
  t=19  P1 finished (was in Q0)
  t=23  P3 finished (was in Q1)

Gantt chart
  +----+----+----+----+----+----+----+----+----+
  | P1 | P2 | P3 | P4 | P1 | P2 | P3 | P1 | P3 |
  +----+----+----+----+----+----+----+----+----+
  0    2    4    6    8    12   14   17   19   23

Results (MLFQ)
  process       arrival    burst    start   finish  turnaround  waiting  response
  P1                  0        8        0       19          19       11         0
  P2                  1        4        2       14          13        9         1
  P3                  2        9        4       23          21       12         2
  P4                  3        2        6        8           5        3         3

  average turnaround time : 14.50
  average waiting time    : 8.75
  average response time   : 1.50
  total time              : 23 (from 0 to 23)
  CPU utilisation         : 100.0% (23 busy of 23)
  throughput              : 0.174 processes per time unit
  context switches        : 8
```

Walking through it:

- **t=0..2** P1 is the only process, runs its 2-unit Q0 slice, uses all of it,
  and is demoted to Q1 (R3).
- **t=2..8** P2, P3 and P4 each get their turn in Q0. P4 only needs 2 units, so
  it **finishes at t=8 while still in the top queue** — the short job never gets
  demoted. Its response time is 3 instead of FCFS's 18.
- **t=8..12** Q0 is empty, so the scheduler drops to Q1 and gives P1 a 4-unit
  slice. It uses all of it and sinks to Q2 (R3).
- **t=15** the boost fires: P1 (sitting in Q2) and the running P3 both return to
  Q0 (R5). This is why P1 finishes at 19 rather than being stuck at the bottom.
- **context switches: 8** against FCFS's 3. That is the price of MLFQ:
  responsiveness costs switching.

`tests/workload2.txt` shows R4 in action:

```
  t=10  LONG used its 4-unit slice in Q1 -> demoted to Q2
  t=12  C arrives, joins Q0
  t=12  LONG preempted (something arrived in Q0)
  t=14  C finished (was in Q0)
```

`LONG` is thrown off the CPU mid-slice because a brand-new process appeared in a
higher queue. Under FCFS, `C` would have had to wait for all 20 units of `LONG`.

---

## 7. FCFS vs MLFQ — what the numbers say

Same workload (`tests/workload1.txt`), same CPU, different scheduler:

| metric                  | FCFS  | MLFQ  | who wins                       |
| ----------------------- | ----- | ----- | ------------------------------ |
| average turnaround time | 14.50 | 14.50 | tie                            |
| average waiting time    | 8.75  | 8.75  | tie                            |
| **average response time** | **8.75** | **1.50** | **MLFQ, by 5.8x**        |
| P4 (shortest job) turnaround | 20 | 5   | MLFQ                           |
| context switches        | 3     | 8     | FCFS                           |
| CPU utilisation         | 100%  | 100%  | tie                            |

And on `tests/workload2.txt`, where two short jobs arrive late while a very long
one is running:

| metric                  | FCFS  | MLFQ |
| ----------------------- | ----- | ---- |
| average turnaround time | 11.20 | 7.40 |
| average waiting time    | 5.80  | 2.00 |
| average response time   | 5.80  | 0.60 |
| context switches        | 4     | 7    |

What to take away:

- **Total work does not change.** The CPU has the same amount of computing to
  do, so with no idle time both schedulers finish the whole batch at t=23. On
  workload 1 the averages of turnaround and waiting come out identical; MLFQ
  moved the pain around (P1 waits longer so P4 waits much less) rather than
  removing it.
- **MLFQ transforms response time**, which is what an interactive user actually
  feels. 8.75 to 1.50 is the difference between a terminal that stutters and one
  that answers instantly.
- **MLFQ helps short jobs at the expense of long ones.** P4's turnaround falls
  from 20 to 5; P1's rises from 8 to 19.
- **Nothing is free.** MLFQ needed 8 context switches instead of 3, and each one
  costs real time in a real kernel (saving registers, swapping page tables,
  losing cache warmth).
- **FCFS is not useless** — it is simple, has almost no overhead, and can never
  starve anyone. It is a bad fit for interactive systems, which is why every
  desktop OS uses something MLFQ-like instead.

Reproduce all of this with:

```bash
./bin/fcfs tests/workload1.txt | tail -12
./bin/mlfq tests/workload1.txt | tail -12
./bin/fcfs tests/workload2.txt | tail -12
./bin/mlfq tests/workload2.txt | tail -12
```

---

## 8. Development log: every command I ran, every problem I hit

This is the honest history of building version 1, kept because the mistakes are
more instructive than the finished code.

### 8.0 Setting up

```bash
mkdir -p src tests bin
gcc --version                     # gcc (Ubuntu 13.3.0) 13.3.0
make --version                    # GNU Make 4.3
```

### 8.1 Problem: `O_RDONLY undeclared`

First compile of the shell:

```bash
gcc -Wall -Wextra -std=c11 -o bin/mysh src/shell.c
```

```
src/shell.c:300:18: warning: implicit declaration of function 'open'; did you mean 'fopen'?
src/shell.c:300:36: error: 'O_RDONLY' undeclared (first use in this function)
src/shell.c:316:21: error: 'O_WRONLY' undeclared (first use in this function)
src/shell.c:316:32: error: 'O_CREAT' undeclared (first use in this function)
```

**Cause.** I used `open()` without including its header. `open()` and every
`O_*` flag come from `<fcntl.h>` — `<stdio.h>` only provides `fopen()`, which is
a different, higher-level thing.

**Fix.**

```c
#include <fcntl.h>      /* open(), O_RDONLY, O_CREAT, O_APPEND, O_TRUNC */
```

**Lesson.** "Implicit declaration of function X" almost always means a missing
`#include`, not a missing library. `man 2 open` names the header in its
SYNOPSIS.

### 8.2 Problem: `implicit declaration of function 'getline'`

The same compile also said:

```
src/shell.c:554:13: warning: implicit declaration of function 'getline'
```

even though `<stdio.h>` was included.

**Cause.** `getline()` is POSIX, not ISO C. Compiling with `-std=c11` puts the
library in strict standard mode, where non-standard functions are hidden. You
have to ask for the POSIX interfaces.

**Fix**, at the very top of `src/shell.c`, **before every `#include`**:

```c
#define _POSIX_C_SOURCE 200809L
```

Placing it after an `#include` has no effect at all, because the headers are
already expanded by then.

**Lesson.** Feature-test macros decide which parts of the C library are visible.
The alternative is `-std=gnu11`, which turns everything on; being explicit is
more honest about what the code needs.

### 8.3 Problem: my own output came out in the wrong order

With both fixes the shell compiled and ran. Testing it non-interactively:

```bash
printf 'echo hello\npwd\nnosuchcommand\nstatus\n' | ./bin/mysh
```

The output was scrambled: everything the *children* printed appeared first, and
the shell's own `pwd` and `status` output turned up at the very end.

**Cause.** C library buffering. When stdout is a terminal it is *line
buffered* (flushed at every `\n`), but when it is a pipe or a file it is *fully
buffered*: about 4 KB is collected before anything is written. My `printf` calls
were sitting in that buffer while the children — separate processes with their
own buffers, writing to the same file — printed immediately.

**Fix**, two lines in two places:

```c
setvbuf(stdout, NULL, _IOLBF, 0);   /* in main(): flush at every newline */
...
fflush(stdout);                     /* in run_pipeline(), just before fork() */
```

The `fflush` before `fork()` matters for a second reason: `fork()` copies the
buffer's *contents* into the child, so unflushed text can be printed twice, once
by each process.

**Verification** — both streams into one file, so the ordering is real:

```bash
printf 'echo A\nnosuchcmd\nstatus\necho B\n' | ./bin/mysh > merged.log 2>&1
cat merged.log
```

```
A
mysh: nosuchcmd: command not found
127
B
```

**Lesson.** stderr is unbuffered, stdout is not. Mixed output that "looks
shuffled" is a buffering bug, not a logic bug.

### 8.4 Problem: quotes did not work at all

The first version split words with `strtok(line, " \t\n")`. Then:

```bash
printf "echo one two three | tr ' ' '\\n' | wc -l\n" | ./bin/mysh
```

```
tr: extra operand ''\\n''
```

**Cause.** `strtok()` only knows about delimiter characters. It cannot see
quotes, so `tr ' ' '\n'` was split into the four "words" `tr`, `'`, `'` and
`'\n'`. The same flaw made `echo "hello   world"` print the quote characters and
collapse the spaces.

**Fix.** I replaced `strtok()` with the hand-written `tokenize()` in §4.2: a
single loop over the characters that copies quoted sections verbatim, handles
backslash escapes, marks operators with `is_op`, and reports an unterminated
quote as a syntax error. It also gained a feature for free — because operator
characters end a word, `ls|wc -l` and `sort<in.txt` now work without spaces.

**Verification.**

```bash
printf 'echo "hello   world"\necho one two three | tr " " "\\n" | wc -l\necho hi|wc -l\n' | ./bin/mysh
```

```
hello   world
3
1
```

**Lesson.** `strtok()` is fine for splitting a CSV line. It is the wrong tool for
anything with quoting rules, and it has two more traps: it modifies the string
in place, and it keeps hidden state, so you cannot use it in nested loops.

### 8.5 Problem: `| wc -l` silently ran instead of failing

Testing bad input against the `strtok()` version:

```bash
printf '| wc -l\nls |\necho done\n' | ./bin/mysh
```

Instead of a syntax error, `wc -l` actually ran. Worse, it read from the
shell's own standard input and started eating the rest of my test script.

**Cause.** `strtok()` skips empty tokens. Splitting `"| wc -l"` on `|` returns
just one piece, `" wc -l"` — the empty first stage disappears, so the shell had
no way to notice that something was missing.

**Fix.** Splitting now happens on the token array, where an empty stage is
visible as "the stage starts where it ends":

```c
if (start == i) {
    fprintf(stderr, "mysh: syntax error near '|'\n");
    return -1;
}
```

**Verification.**

```bash
printf '| wc -l\nls | | wc -l\nls |\necho x >\necho "oops\nsleep 1 & echo hi\n' | ./bin/mysh
```

```
mysh: syntax error near '|'
mysh: syntax error near '|'
mysh: syntax error near '|'
mysh: syntax error: expected a file name after '>'
mysh: syntax error: unterminated " quote
mysh: syntax error near '&' (it must be the last thing on the line)
```

**Lesson.** A parser that cannot represent "nothing is here" cannot reject it.
Always test the *invalid* inputs; this bug only showed up because I typed
nonsense on purpose.

### 8.6 Experiment: what happens if the parent does not close the pipe

I wanted proof of why `close(fd[1])` matters, so I broke a copy of the shell on
purpose:

```bash
cp src/shell.c /tmp/broken_shell.c
# remove the parent's close(fd[1]) in run_pipeline()
gcc -Wall -Wextra -std=c11 -o /tmp/broken_shell /tmp/broken_shell.c
echo 'echo hello | wc -l' | timeout 5 /tmp/broken_shell
echo "exit code: $?"
```

```
exit code: 124        <- 124 means timeout killed it: the shell hung forever
```

With the `close(fd[1])` restored, the same command prints `1` instantly.

**Why.** After `fork()`, both the parent and the child hold the write end of the
pipe. `echo` exits and closes its copy, but the parent still has one open, so as
far as the kernel is concerned more data could still arrive. `wc -l` therefore
waits for end-of-file that never comes, and the parent waits for `wc` — a
deadlock.

**Lesson.** Close every descriptor you are not going to use, in **both**
processes, immediately after `fork()`. This one line is the most common bug in
student shells.

### 8.7 Problem: a test assertion that was wrong, not the code

```bash
make test
```

```
  FAIL  the workload can be read from standard input
        expected: [1]
        actual:   [4]
```

**Cause.** My check was `printf 'ONE 0 1\n' | ./bin/fcfs - | grep -c 'ONE'`.
The name `ONE` legitimately appears four times in the output (workload table,
timeline, results table, Gantt chart), so `grep -c` returned 4. The program was
right; the test was lazy.

**Fix.** Assert on something meaningful instead of counting name occurrences:

```bash
contains "the workload can be read from standard input" \
   "average turnaround time : 1.00" \
   "$(printf 'ONE 0 1\n' | $FCFS_BIN -)"
```

**Lesson.** When a test fails, first ask whether the test is wrong. Assertions
should check behaviour, not incidental text.

### 8.8 Problem: a comment that described output the program never produced

`tests/workload2.txt` originally claimed "the CPU is idle between time 4 and
12". Running it disproved that immediately — the long process `LONG` covers that
whole span, so utilisation is 100%.

**Fix.** I corrected the comment to describe what actually happens (preemption
at t=12 and the priority boost at t=15) and added `tests/workload3.txt`, which
really does leave the CPU idle:

```bash
./bin/fcfs tests/workload3.txt
```

```
  +---+------+---+---+
  | X | idle | Y | Z |
  +---+------+---+---+
  0   2      8   11  12
  ...
  CPU utilisation         : 50.0% (6 busy of 12)
```

**Lesson.** Verify comments against real output. A confidently wrong comment is
worse than no comment.

### 8.9 Small cleanups along the way

- Renamed a local variable from `new` to `next_q` in `mlfq.c`. It is legal in C
  but a reserved word in C++, and gratuitous incompatibility is not worth it.
- Replaced `atoi()` with `sscanf()`-based parsing plus range checks in the
  workload loader, so bad input is reported instead of silently becoming 0.
- Used `%11s` in the workload `sscanf()` so a very long process name cannot
  overflow `name[12]`.

### 8.10 Checking for memory errors

`valgrind` is not installed in this environment, so I used the compiler's
sanitizers instead, which catch buffer overflows, use-after-free, leaks and
undefined behaviour at run time:

```bash
gcc -Wall -Wextra -std=c11 -g -fsanitize=address,undefined -o /tmp/san/mysh src/shell.c
gcc -Wall -Wextra -std=c11 -g -fsanitize=address,undefined -o /tmp/san/fcfs src/fcfs.c src/scheduler_common.c
gcc -Wall -Wextra -std=c11 -g -fsanitize=address,undefined -o /tmp/san/mlfq src/mlfq.c src/scheduler_common.c

printf 'echo hi\nls | wc -l\ncd /tmp\npwd\nnosuch\nstatus\n| wc\necho "x\nexit 3\n' | /tmp/san/mysh
/tmp/san/fcfs tests/workload1.txt > /dev/null
/tmp/san/mlfq tests/workload2.txt > /dev/null
printf 'A 0 1\n' | /tmp/san/fcfs -   > /dev/null
/tmp/san/fcfs /nope 2>/dev/null
```

No sanitizer reports and no leaks, including on the error paths. If you have
valgrind available, `valgrind --leak-check=full ./bin/fcfs tests/workload1.txt`
gives the same answer.

### 8.11 The full command list, in order

```bash
mkdir -p src tests bin
gcc -Wall -Wextra -std=c11 -o bin/mysh src/shell.c          # failed  (§8.1, §8.2)
gcc -Wall -Wextra -std=c11 -o bin/mysh src/shell.c          # clean
printf 'echo hello\npwd\nstatus\n' | ./bin/mysh             # found the ordering bug (§8.3)
printf 'echo A\nnosuchcmd\nstatus\necho B\n' | ./bin/mysh > merged.log 2>&1
python3   # inline pty script, shown below: Ctrl-C, background jobs (§4.9)
make                                                        # builds all three programs
./bin/fcfs tests/workload1.txt
./bin/mlfq tests/workload1.txt
./bin/mlfq tests/workload2.txt                              # preemption + boost
./bin/fcfs tests/workload3.txt                              # idle CPU
chmod +x tests/run_tests.sh
make test                                                   # 45 pass, 1 bad test (§8.7)
make test                                                   # 46 pass
gcc ... -fsanitize=address,undefined ...                    # memory checks (§8.10)
```

The interactive Ctrl-C test needs a real terminal, which a pipe cannot provide,
so it was driven through a pseudo-terminal:

```python
import os, pty
pid, fd = pty.fork()
if pid == 0:
    os.execv("./bin/mysh", ["./bin/mysh"])
os.write(fd, b"sleep 30\n")
os.write(fd, b"\x03")          # Ctrl-C
os.write(fd, b"status\n")      # prints 130 = 128 + SIGINT
```

---

## 9. Testing

```bash
make test
```

46 checks, all passing:

```
=== shell: running commands ===        4 checks   arguments, quoting, PATH lookup
=== shell: pipes ===                   3 checks   2-stage, 3-stage, no-space pipes
=== shell: redirection ===             4 checks   >, >>, <, redirection with a pipe
=== shell: error handling ===         12 checks   127, 126, 1, syntax errors, bad paths
=== shell: built-ins ===               4 checks   cd, exit code, help, comments
=== shell: background jobs ===         1 check    pid is reported
=== FCFS ===                           5 checks   order, averages, utilisation, idle block
=== MLFQ ===                           5 checks   demotion, boost, preemption, response time
=== scheduler input validation ===     8 checks   missing file, bad fields, --help, stdin
-------------------------------------
passed: 46   failed: 0
```

The script is plain bash with two helpers — `ok` compares exact strings and
`contains` looks for a substring — so adding a test is three lines. It exits
non-zero if anything fails, so it can drop straight into CI later.

Manual checks worth doing yourself, because they need a real terminal:

```bash
./bin/mysh
mysh$ sleep 30          # then press Ctrl-C: sleep dies, the shell survives
mysh$ status            # 130 = 128 + SIGINT
mysh$ sleep 2 &         # prints [background] pid N
mysh$ echo hi           # the shell is still usable
mysh$                   # a moment later: [done] pid N exited with status 0
mysh$                   # press Ctrl-D to leave
```

---

## 10. Questions you may be asked, with answers

**Why can `cd` not be an external program?**
`fork()` gives the child a copy of the parent's state. A child that calls
`chdir()` changes only its own directory and then exits. The shell must call
`chdir()` in its own process, so `cd` has to be built in.

**What does `fork()` return?**
0 in the child, the child's pid in the parent, and -1 on failure. It is the only
way each process can tell which one it is.

**What happens if `execvp()` succeeds?**
Nothing "happens" in your code, because your code is gone — the process image
has been replaced. That is why any statement after `execvp()` is error handling.

**Difference between `exit()` and `_exit()`?**
`exit()` runs `atexit` handlers and flushes stdio buffers; `_exit()` ends the
process immediately. After `fork()`, a child that inherits the parent's unflushed
buffer must use `_exit()`, or that output gets printed twice.

**What is a zombie, and how do you avoid one?**
A finished process whose exit status has not been collected. The parent removes
it by calling `wait()`/`waitpid()`. This shell calls `waitpid(-1, ..., WNOHANG)`
before each prompt for background jobs.

**Why must the parent close the pipe's write end?**
A reader sees end-of-file only when *every* copy of the write end is closed.
`fork()` created a second copy in the parent, so if the parent keeps it, the
reader blocks forever (demonstrated in §8.6).

**How does `2>&1`-style redirection work, and why is `>` different from `>>`?**
Redirection is `dup2()`: make a standard descriptor refer to another open file.
`>` opens with `O_TRUNC` (empty the file first), `>>` opens with `O_APPEND`
(always write at the end).

**Why is FCFS bad for interactive systems?**
It is non-preemptive, so one long process blocks everything behind it (the
convoy effect). In `tests/workload1.txt`, P4 needs 2 units of CPU and waits 18.

**What is MLFQ actually learning?**
Which processes are CPU-bound. A process that consumes an entire time slice is
demoted, so long jobs sink and short, interactive jobs stay on top — without
anyone declaring in advance which is which.

**What is starvation, and how does MLFQ prevent it?**
A low-priority process never getting the CPU because higher queues keep
refilling. The periodic **priority boost** (rule R5) moves everything back to Q0
every 15 time units, guaranteeing progress.

**Why does the lowest queue have the largest time slice?**
Processes there are long-running, so longer slices mean fewer context switches
per unit of useful work.

**Turnaround vs waiting vs response time?**
Turnaround = finish - arrival. Waiting = turnaround - burst. Response =
first-run - arrival. MLFQ's big win in this project is response time: 8.75 down
to 1.50.

**Why is the MLFQ average turnaround time the same as FCFS here?**
With no idle CPU, the batch takes the same total time either way. MLFQ
redistributes the waiting — the short job P4 goes from 20 to 5, the long job P1
from 8 to 19 — and pays 8 context switches instead of 3.

---

## 11. Limitations and ideas for version 2

Known limitations of version 1, all deliberate:

- **Shell:** no `$VAR` expansion, no globbing (`*.c`), no `&&`/`||`/`;`, no
  `2>`/`2>&1`, no here-documents (`<<`), no command substitution, no
  `jobs`/`fg`/`bg`, no arrow-key history, and built-ins are only recognised as a
  single command (not inside a pipeline). Fixed limits: 128 tokens, 64 arguments,
  16 pipeline stages.
- **Schedulers:** a single CPU, integer time units, and processes that are pure
  CPU (no I/O bursts, so nothing ever blocks). MLFQ's queue count, time slices
  and boost interval are compile-time constants.

Version 2, roughly in order of usefulness:

1. `$?`, `$HOME` and other variable expansion in the shell, plus `export`.
2. `&&`, `||` and `;` so commands can be chained conditionally.
3. Real job control: `jobs`, `fg`, `bg`, process groups and `tcsetpgrp()`, so
   Ctrl-Z works.
4. `2>` and `2>&1` for redirecting standard error.
5. Glob expansion with `glob(3)`.
6. Schedulers: I/O bursts, so a process can block and MLFQ can reward the
   interactive behaviour it is actually designed to detect.
7. Command-line options for the schedulers (`--queues`, `--quantum`,
   `--boost`) and a CSV export for plotting.
8. A combined comparison mode that runs both algorithms on one workload and
   prints a single table.

---

## Build details

```bash
gcc -Wall -Wextra -std=c11 -g -o bin/mysh src/shell.c
gcc -Wall -Wextra -std=c11 -g -o bin/fcfs src/fcfs.c src/scheduler_common.c
gcc -Wall -Wextra -std=c11 -g -o bin/mlfq src/mlfq.c src/scheduler_common.c
```

| flag              | why                                                     |
| ----------------- | ------------------------------------------------------- |
| `-Wall -Wextra`   | turn on the warnings that catch real bugs               |
| `-std=c11`        | compile against the 2011 C standard                     |
| `-g`              | keep debug symbols so `gdb` and the sanitizers are useful |

The code compiles with **zero warnings** under those flags on gcc 13.3, and
targets Linux (it uses POSIX: `fork`, `execvp`, `pipe`, `dup2`, `waitpid`).
