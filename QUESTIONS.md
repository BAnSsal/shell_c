# Placement Question Bank — Linux Shell & CPU Scheduling Project

Every question an interviewer can reasonably ask about this project, from
"what does `fork()` return" to "trace MLFQ by hand", with answers.

**How to use this file**

1. Read `README.md` first — this file assumes you know what the code does.
2. Cover the answers and try to say them out loud. Interviews are spoken, and an
   answer you can only write is an answer you do not have.
3. Anything marked **[trap]** is a question designed to catch someone who copied
   the project instead of writing it. Those are the ones worth over-preparing.
4. Numbers in the answers come from actually running the programs, so you can
   verify any of them yourself: `./bin/sched compare tests/workload1.txt`.

**Contents**

| part | topic | questions |
| ---- | ----- | --------- |
| [A](#part-a--presenting-the-project) | Presenting the project | 8 |
| [B](#part-b--c-programming-basics) | C programming basics | 22 |
| [C](#part-c--processes-and-fork) | Processes and `fork()` | 14 |
| [D](#part-d--the-exec-family) | The `exec` family | 10 |
| [E](#part-e--wait-zombies-and-orphans) | `wait`, zombies, orphans | 12 |
| [F](#part-f--file-descriptors-and-redirection) | File descriptors, redirection | 14 |
| [G](#part-g--pipes-and-ipc) | Pipes and IPC | 14 |
| [H](#part-h--signals-and-the-terminal) | Signals and the terminal | 13 |
| [I](#part-i--shell-implementation-deep-dive) | Shell implementation deep dive | 16 |
| [J](#part-j--parsing-and-the-tokenizer) | Parsing and the tokenizer | 10 |
| [K](#part-k--scheduling-fundamentals) | Scheduling fundamentals | 18 |
| [L](#part-l--fcfs) | FCFS | 7 |
| [M](#part-m--sjf-and-srtf) | SJF and SRTF | 12 |
| [N](#part-n--round-robin) | Round Robin | 10 |
| [O](#part-o--priority-scheduling) | Priority scheduling | 8 |
| [P](#part-p--mlfq) | MLFQ | 14 |
| [Q](#part-q--numerical-problems-with-full-solutions) | Numerical problems | 5 solved |
| [R](#part-r--data-structures-and-algorithms-used) | Data structures used | 9 |
| [S](#part-s--testing-debugging-and-tooling) | Testing, debugging, tooling | 12 |
| [T](#part-t--design-and-code-quality) | Design and code quality | 10 |
| [U](#part-u--spot-the-bug) | Spot the bug | 6 |
| [V](#part-v--extend-the-project) | Extend the project | 9 |
| [W](#part-w--rapid-fire) | Rapid fire | 70 |
| [X](#part-x--honest-limitations) | Honest limitations | 8 |
| [Y](#part-y--live-coding-tasks) | Live coding tasks | 10 |
| [Z](#part-z--hr-and-behavioural) | HR and behavioural | 8 |

---

## Part A — Presenting the project

**A1. Describe the project in 60 seconds.**

I built two programs in C. The first is a Unix-like shell: it reads a command
line, splits it into words and operators, and runs the commands using `fork()`
and `execvp()`. It supports pipes of any length, input/output redirection,
background jobs, five built-in commands and bash-compatible exit codes. The
second is a CPU scheduling simulator with six algorithms — FCFS, SJF, SRTF,
Round Robin, Priority and MLFQ — which prints a Gantt chart, a per-process
timeline and the standard metrics, and has a `compare` mode that runs all six on
one workload so the trade-offs are visible in a single table. Both are plain C11
with no dependencies, about 2,200 lines, and 87 automated tests.

**A2. Why did you build both a shell and a scheduler in one project?**

They are the two halves of the same idea. The shell is *how* processes get
created — `fork`, `exec`, `wait`, file descriptors. The scheduler is *what the
kernel does with them* once they exist. Writing the shell taught me the
mechanism; writing the simulator taught me the policy.

**A3. What was the hardest part?**

The pipe cleanup. Getting `echo hi | wc -l` to work is easy; getting it to
*terminate* is not. A reader only sees end-of-file when every copy of the write
end is closed, and `fork()` silently duplicates them. I proved it to myself by
deleting the parent's `close(fd[1])` on a copy of the shell — the command then
hung forever and `timeout` had to kill it with exit code 124.

**A4. What did you learn that you did not expect?**

That C library buffering can make correct code look broken. My shell's own
output appeared after the output of the programs it started, because when stdout
is a pipe it is fully buffered (~4 KB) while children write immediately. Fixed
with `setvbuf(stdout, NULL, _IOLBF, 0)` and an `fflush(stdout)` before every
`fork()`.

**A5. Which part are you proudest of?** **[trap]**

The `compare` mode, because of a bug I avoided rather than fixed. The algorithms
consume the workload — `remaining` counts down to zero and FCFS sorts the array
— so running six algorithms over one array would make every algorithm after the
first "finish" instantly and print plausible but completely wrong averages. Each
one gets a `memcpy` copy, and there is a test asserting that all six report the
same total time, since the same work must take the same CPU time.

**A6. How did you verify the simulator is correct?**

I computed workload 1 by hand on paper before running anything, and the program
matched all six algorithms exactly, including the context-switch counts. On top
of that there are two property-based checks that do not depend on my arithmetic:
Round Robin with a quantum larger than every burst must reproduce FCFS exactly,
and Priority scheduling on a workload with no priority column must also
reproduce FCFS. Both are in the test suite.

**A7. How long is the code, and how would I find my way around it?**

`src/shell.c` is one file of about 690 lines in five labelled parts: tokenizer,
parser, built-ins, execution, main loop. The simulator is one file per
algorithm (`src/algo_fcfs.c`, `algo_sjf.c`, `algo_srtf.c`, `algo_rr.c`,
`algo_priority.c`, `algo_mlfq.c`), with the shared workload loading, metrics and
printing in `scheduler_common.c` and the command line in `sched.c`.

**A8. If you had two more weeks, what would you add?**

For the shell, real job control: `setpgid()` to put each pipeline in its own
process group, `tcsetpgrp()` to hand over the terminal, SIGTSTP handling and
`jobs`/`fg`/`bg`. That is the biggest functional gap — right now Ctrl-Z stops my
shell along with the command. For the simulator, I/O bursts, so processes can
block and MLFQ can reward the interactive behaviour it is actually designed to
detect.

---

## Part B — C programming basics

**B1. Why `#define _POSIX_C_SOURCE 200809L` at the top of `shell.c`?**

`getline()` is POSIX, not ISO C. Compiling with `-std=c11` puts glibc in strict
standard mode where non-standard functions are hidden, so `getline` produced
"implicit declaration". The macro asks for the POSIX 2008 interfaces. It must
appear before every `#include`, otherwise the headers are already expanded and
it does nothing.

**B2. What does "implicit declaration of function" mean?**

The compiler saw a call to a function it has no declaration for. In C99 and
later that is an error or a serious warning; the compiler has to guess the
signature, which breaks silently at link time or run time. It almost always
means a missing `#include` — for `open()` it was `<fcntl.h>`.

**B3. Why `getline()` instead of `gets()` or `scanf("%s")`?**

`gets()` has no bounds at all and was removed from C11; `scanf("%s")` stops at
whitespace and can overflow the buffer. `getline()` allocates and grows the
buffer itself, so there is no fixed line length and no overflow. It returns -1
at end of input, which is how Ctrl-D exits the shell.

**B4. What is the difference between `char *argv[]` and `char **argv`?**

Nothing, as a function parameter — an array parameter decays to a pointer. As a
declaration inside a function they differ: `char *a[10]` allocates ten pointers,
`char **a` is a single pointer.

**B5. Why must `argv` end with `NULL` for `execvp()`?**

`execvp` has no length parameter, so `NULL` is the only way it can find the end
of the list. Forget it and it walks off into garbage memory. In this project
`parse_command()` ends with `cmd->words[cmd->nwords] = NULL;` for exactly that
reason.

**B6. In `struct command`, `words[]` points into the token array. What is the
risk?** **[trap]**

Dangling pointers. Nothing is copied, so those pointers are only valid while the
token array is alive. That is safe here because `toks[]` and `cmds[]` are both
locals of `execute_line()` and die together, but if the command were ever stored
for later — a history feature, say — the strings would have to be `strdup`'d.

**B7. Why `memset(cmd, 0, sizeof *cmd)` in `parse_command()`?**

Automatic variables in C hold garbage, not zeros. Without clearing the struct,
`infile`/`outfile` could hold leftover pointers from a previous command and the
shell would redirect from a file the user never mentioned.

**B8. Why `sizeof *cmd` rather than `sizeof(struct command)`?**

It stays correct if the type of `cmd` changes, and it cannot get out of step with
the variable it is used on.

**B9. What does `static` mean on a function like `static int tokenize(...)`?**

Internal linkage: the name is visible only inside that translation unit. It
keeps the file's private helpers out of the global namespace and lets the
compiler optimise or inline more freely.

**B10. What does `const` mean in `const struct process procs[]`?**

The function promises not to modify the array through that pointer. It documents
intent and lets the compiler catch an accidental write — useful for
`pick_shortest()`, which only reads.

**B11. Why is the process array `struct process procs[MAX_PROCS]` and not
`malloc`'d?**

The maximum is small and known (16), so a fixed array avoids allocation failure
paths and freeing entirely. Every index is bounds-checked at load time. With an
unknown or large bound, `malloc` would be right.

**B12. Where can this code fail to compile as C++, and why does that matter?**

I originally used `new` as a variable name in the MLFQ demotion code. It is
legal C but a reserved word in C++, so I renamed it `next_q`. It matters because
gratuitous incompatibility costs nothing to avoid.

**B13. What is the difference between `exit()` and `_exit()`?**

`exit()` runs `atexit` handlers and flushes stdio buffers; `_exit()` ends the
process immediately. After `fork()` a child inherits a *copy* of the parent's
unflushed buffer, so a child that calls `exit()` can print the parent's pending
output a second time. Children in this shell use `_exit()`.

**B14. What is the return value of `main()` used for?**

It becomes the process's exit status. `mysh` returns the code from `exit N`, or
the last command's status, so `./bin/mysh < script.sh; echo $?` behaves like a
real shell script run.

**B15. What does `sizeof demo / sizeof demo[0]` compute?**

The number of elements in the array `demo`. Used in `load_demo_workload()` so
that adding a process to the built-in workload cannot desynchronise the count.

**B16. Why `%11s` in `sscanf(line, "%11s %d %d %d", ...)`?**

`name[NAME_LEN]` is 12 bytes, and `%11s` writes at most 11 characters plus the
terminating NUL. A bare `%s` has no bound and is a buffer overflow waiting for a
long process name.

**B17. What does `sscanf` return, and why does the code check for 3 *or* 4?**

The number of items successfully converted. The priority column is optional, so
3 means "no priority given" (default 0) and 4 means "priority given". Anything
else is a malformed line.

**B18. Why `strcmp(a, b) == 0` and not `a == b` for strings?**

`a == b` compares pointers, which are almost never equal even for identical
text. `strcmp` compares the characters and returns 0 when they match.

**B19. What is the difference between `strcpy`, `strncpy` and `snprintf` here?**

`strcpy` has no bound. `strncpy` bounds the copy but does not guarantee a
terminating NUL. `snprintf(dst, size, "%s", src)` bounds it *and* always
terminates, which is why the workload loader uses it.

**B20. Explain `while ((pid = waitpid(-1, &wstatus, WNOHANG)) > 0)`.**

Assignment inside the condition: call `waitpid`, store the result in `pid`, and
loop while it is positive (a child was reaped). 0 means "children exist but none
finished", -1 means "no children left" — both end the loop.

**B21. What is an order-only prerequisite in the Makefile (`| $(BIN)`)?**

It guarantees `bin/` exists before linking, but changes to the directory's
timestamp do not trigger a rebuild. Without the `|`, every file added to `bin/`
would relink everything.

**B22. Why compile with `-Wall -Wextra`?**

They turn on the warnings that catch real bugs: unused results, sign mismatches,
uninitialised use, missing field initialisers. This project compiles with zero
warnings under both, plus `-std=c11`, and I treat a new warning as a bug.

---

## Part C — Processes and `fork()`

**C1. What is a process?**

A program in execution, together with the state the kernel keeps for it: its
address space, open file descriptors, current directory, credentials, pending
signals, and a scheduling state. Identified by a pid.

**C2. What does `fork()` return?**

0 in the child, the child's pid in the parent, and -1 on failure (with `errno`
set, typically `EAGAIN` when a process limit is hit or `ENOMEM`). The return
value is the only thing distinguishing two otherwise identical processes.

**C3. What does the child inherit from the parent?**

A copy of the address space, the open file descriptors (sharing the same open
file descriptions, so the same file offsets), the current directory, the umask,
the environment, the process group and session, and signal dispositions.
It gets a new pid, its ppid is the parent, its pending signal set is cleared,
and its CPU time counters restart.

**C4. Is memory really copied on `fork()`?**

No — Linux uses copy-on-write. The page tables are duplicated with pages marked
read-only, and a page is copied only when one of the two processes writes to it.
That is what makes `fork()` cheap enough to do per command.

**C5. What is the difference between `fork()`, `vfork()` and `clone()`?**

`clone()` is the actual Linux system call; `fork()` is a thin wrapper over it.
`vfork()` suspends the parent and shares its memory until the child execs — an
optimisation from before copy-on-write, now discouraged. `clone()` with flags is
also how threads are created.

**C6. Why do you flush stdout before `fork()`?**

The child inherits a copy of whatever is still in the buffer, so anything
unflushed can be printed twice — once by each process. Flushing first guarantees
the parent's output is written exactly once and in the right order.

**C7. What happens to `errno` in the child?**

It is a copy like everything else. In this code the child sets it by failing
`execvp`, and that value never affects the parent.

**C8. What is a fork bomb, and does your shell allow one?**

A process that forks endlessly until the process table fills. My shell forks once
per command, but nothing stops the *user* from running a fork bomb — that is
what `RLIMIT_NPROC` and cgroups exist for, not the shell.

**C9. Your shell forks even for `ls`. Is that wasteful?**

It is unavoidable: `exec` replaces the calling process, so without forking first
the shell itself would become `ls` and never come back. That is exactly why
`cd`, `pwd`, `exit`, `help` and `status` are built-ins instead.

**C10. What is `getpid()` versus `getppid()`?**

Your own pid, versus your parent's. If the parent has died, `getppid()` returns
1 (or a subreaper's pid), because orphans are reparented.

**C11. How many processes does `a | b | c` create in your shell?**

Three children, one per stage, plus the shell itself. The pipeline is not nested:
the shell is the parent of all three.

**C12. Do the children of a pipeline run concurrently?**

Yes. All of them are forked before the shell waits for any of them, so `sort`
can consume `cat`'s output as it is produced rather than after it finishes. If
the shell waited for each stage before forking the next, a pipe full of data
would deadlock.

**C13. What is the maximum number of processes your shell can create at once?**

Sixteen per pipeline (`MAX_CMDS`), plus any background jobs still running. The
limit is a compile-time constant and reported as a clear error rather than a
crash.

**C14. What is the difference between a process and a thread?** **[trap]**

Threads share one address space and file descriptor table; processes do not.
Sharing makes communication free and synchronisation your problem. This project
uses processes only — that is what a shell does — but the scheduling concepts
apply to threads too, since Linux schedules threads, not processes.

---

## Part D — The `exec` family

**D1. What does `exec` do?**

Replaces the program running inside the current process with a new one. The pid,
ppid, open file descriptors, cwd and process group stay the same; the code, data
and stack are thrown away and rebuilt from the new executable.

**D2. Why is there no "success" return from `execvp`?**

On success the calling code no longer exists, so there is nothing to return to.
Any line after `execvp` runs only if it failed — which is why the next statement
in this project is error handling.

**D3. Explain the naming of `execl`, `execv`, `execlp`, `execvp`, `execle`,
`execvpe`.**

`l` = arguments as a **l**ist of parameters; `v` = arguments as a **v**ector
(array); `p` = search `PATH`; `e` = pass an explicit **e**nvironment. So
`execvp` takes an array and searches PATH, which is what a shell wants.

**D4. Which one is the real system call?**

`execve()`. All the others are library wrappers around it.

**D5. How does `execvp` find `ls`?**

It walks the colon-separated directories in `PATH` and tries each one. If the
name contains a `/` it is used directly with no search.

**D6. What survives `exec` and what does not?**

Survives: pid, ppid, open file descriptors (unless `FD_CLOEXEC` is set), cwd,
umask, process group and session, resource limits, and *ignored* signal
dispositions. Does not survive: the memory image, and any signal *handlers* you
installed — they are reset to default, because the handler function no longer
exists.

**D7. Then why does your shell reset SIGINT to `SIG_DFL` in the child?**
**[trap]**

Because *ignoring* a signal does survive `exec`, unlike a handler. The shell sets
`SIGINT` to `SIG_IGN`, and without resetting it the child would inherit that and
you would have an un-killable `sleep 100`. This is the difference between
`SIG_IGN` and a handler, and it is a favourite interview detail.

**D8. Why exit code 127 for "command not found" and 126 for "not executable"?**

Convention followed by every POSIX shell, so scripts can distinguish the cases.
127 corresponds to `ENOENT` from `execvp`; 126 to the file existing but not being
runnable (a directory, or no execute bit).

**D9. How do you distinguish "no such file" from "is a directory"?**

`errno`. `ENOENT` means the name does not exist. `EACCES` on something that
`stat()` reports as a directory means the user typed a directory name, so the
message is "is a directory" and the code is 126.

**D10. Does `execvp` run shell built-ins like `cd`?**

No. Built-ins are not programs on disk, so there is nothing for `exec` to load.
That is why the shell must implement them itself.

---

## Part E — `wait`, zombies and orphans

**E1. Why must a parent call `wait()`?**

To collect the child's exit status. Until it does, the kernel keeps the dead
child's entry in the process table so the status is not lost.

**E2. What is a zombie process?**

A process that has terminated but whose status has not been reaped. It holds no
memory or CPU — only a process table entry — but enough of them exhaust the pid
space.

**E3. How do you get rid of a zombie?**

The parent calls `wait()`/`waitpid()`. If the parent has already exited, the
zombie is reparented to init/systemd, which reaps it automatically. You cannot
kill a zombie with a signal — it is already dead.

**E4. What is an orphan process?**

One whose parent exited first. It is reparented to PID 1 (or the nearest
subreaper), which will reap it when it dies.

**E5. How does your shell avoid zombies from background jobs?**

`reap_background()` runs before every prompt:

```c
while ((pid = waitpid(-1, &wstatus, WNOHANG)) > 0)
    printf("[done] pid %d exited with status %d\n", pid, status_to_code(wstatus));
```

`-1` means any child, and `WNOHANG` means do not block if none has finished. The
loop matters because several may have finished since the last prompt.

**E6. What is the difference between `wait()` and `waitpid()`?**

`wait(&st)` is exactly `waitpid(-1, &st, 0)`: block until *any* child changes
state. `waitpid` can target a specific pid or process group and takes options
(`WNOHANG`, `WUNTRACED`, `WCONTINUED`).

**E7. Explain the `wstatus` macros.**

`WIFEXITED` — terminated normally, with `WEXITSTATUS` giving the code.
`WIFSIGNALED` — killed by a signal, with `WTERMSIG` giving the signal.
`WIFSTOPPED` — stopped, with `WSTOPSIG`. `WIFCONTINUED` — resumed. The status is
a packed integer, so reading it directly is wrong.

**E8. Why does a signal-killed process report 128 + N?**

Shell convention so a single number can express both cases. Ctrl-C is SIGINT = 2,
so it becomes 130 — which is exactly what `status` prints in my shell after
interrupting `sleep 30`.

**E9. In a pipeline, whose exit status does the shell report?**

The last command's. `false | true` succeeds and `true | false` fails, matching
bash. Bash's `PIPESTATUS` array and `set -o pipefail` exist because that default
hides failures.

**E10. Why does your wait loop use `started` rather than `ncmds`?** **[trap]**

Because a failed `pipe()` or `fork()` can stop the loop early. Waiting for a
child that was never created would block forever, so the code waits only for the
ones it actually started.

**E11. What is SIGCHLD, and why does your shell not use it?**

The kernel sends SIGCHLD to the parent whenever a child terminates, stops or
continues. A production shell installs a handler to reap asynchronously. Mine
polls with `WNOHANG` at each prompt instead, which is simpler, has no async-signal
safety problems, and is enough for a project of this size — at the cost of only
noticing a finished job at the next prompt.

**E12. What is the double-fork trick?**

Fork, then fork again in the child and exit the intermediate parent. The
grandchild is immediately orphaned and reparented to init, so nobody has to wait
for it. It is how daemons detach.

---

## Part F — File descriptors and redirection

**F1. What is a file descriptor?**

A small non-negative integer indexing a per-process table of open files. 0, 1
and 2 are standard input, output and error by convention.

**F2. Describe the three levels of the kernel's file structures.**

Per-process **file descriptor table** → **open file description** (holds the
current offset and status flags) → **inode** (the file itself). Two descriptors
can point at the same open file description (via `dup`) and therefore share an
offset, or at different descriptions of the same inode and have independent
offsets.

**F3. What does `dup2(oldfd, newfd)` do?**

Makes `newfd` refer to the same open file description as `oldfd`, closing
`newfd` first if it was open. That is the whole mechanism of redirection.

**F4. Walk through `> out.txt` in your shell.**

In the child, before exec: `open("out.txt", O_WRONLY|O_CREAT|O_TRUNC, 0644)`
returns a new descriptor, say 3; `dup2(3, STDOUT_FILENO)` makes fd 1 refer to the
file; `close(3)` drops the spare copy. The program then writes to fd 1 as usual
and never knows it is a file.

**F5. Why `close(fd)` after `dup2`?**

The duplicate on fd 1 keeps the file open, so the original is redundant. Leaving
it open leaks a descriptor into every command the shell runs.

**F6. What is the only difference between `>` and `>>`?**

`O_TRUNC` versus `O_APPEND`. Truncate empties an existing file at open time;
append makes every write go to the end of the file, atomically with respect to
other appending writers.

**F7. Why is redirection applied in the child and not the parent?**

Because it must affect *only* the command. If the shell redirected its own fd 1,
every later prompt and message would go to the file too.

**F8. Why is redirection applied after the pipe wiring?**

So the file wins when both are present. In `ls | wc -l > out.txt`, `wc`'s stdout
is first pointed at the pipe by the pipeline code and then overwritten by
`dup2` to the file — which is the behaviour bash has.

**F9. What does `0644` mean, and does the file really get those permissions?**

Owner read+write, group and others read. The actual mode is `0644 & ~umask`, so
with the common umask 022 it is 0644, but a stricter umask makes it stricter.
The mode argument only matters when `O_CREAT` actually creates the file.

**F10. What happens if `open()` fails in the child?**

The shell prints `mysh: <file>: <reason>` from `strerror(errno)` and the child
exits with status 1 — it must not go on to `exec`, or the command would run with
the wrong input or output.

**F11. How would you implement `2> err.log` and `2>&1`?**

`2> err.log` is the same code with `STDERR_FILENO` instead of `STDOUT_FILENO`.
`2>&1` is `dup2(STDOUT_FILENO, STDERR_FILENO)` — no `open` at all. Order
matters: `cmd > f 2>&1` sends both to the file, while `cmd 2>&1 > f` sends stderr
to the old stdout and only stdout to the file.

**F12. Why does `echo hi > file` create the file even if `echo` fails?**

Redirection is set up before the program runs, and `O_CREAT` happens at that
moment. This is also why `> file` on its own truncates a file in bash.

**F13. What is `FD_CLOEXEC`?**

A per-descriptor flag that closes the descriptor automatically on `exec`. It is
how a program keeps private descriptors from leaking into children — the
alternative to closing everything by hand after `fork`.

**F14. Are file descriptors inherited across `exec`?**

Yes, unless `FD_CLOEXEC` is set. That is precisely why redirection works: the
child sets up fds 0/1/2 and `exec` keeps them.

---

## Part G — Pipes and IPC

**G1. What is a pipe?**

A unidirectional in-kernel byte buffer with two descriptors: `fd[0]` to read and
`fd[1]` to write. Anonymous pipes are shared by inheritance, so only related
processes can use them.

**G2. What is the pipe's capacity, and what happens when it is full?**

64 KB by default on Linux. A writer blocks when the buffer is full and a reader
blocks when it is empty; that back-pressure is what keeps a pipeline in step
without any explicit synchronisation.

**G3. When does a reader see end-of-file?**

Only when *every* copy of the write end is closed. This is the single most
important fact about pipes, and the reason a shell must close its own copies
after forking.

**G4. What did you observe when you deliberately removed `close(fd[1])`?**

`echo hello | wc -l` hung forever, and `timeout 5` killed the shell with exit
code 124. `echo` finished and closed its copy, but the parent still held one, so
`wc` waited for input that could never arrive while the shell waited for `wc`.

**G5. What happens if you write to a pipe with no reader?**

The writer gets `SIGPIPE`, whose default action is to kill it. If SIGPIPE is
ignored or handled, `write()` returns -1 with `EPIPE`. That is why `yes | head -1`
terminates instead of running forever.

**G6. Draw the descriptor plumbing for `a | b | c`.**

Two pipes. `a`'s stdout → pipe1's write end; `b`'s stdin → pipe1's read end;
`b`'s stdout → pipe2's write end; `c`'s stdin → pipe2's read end. After forking
each child the parent closes both ends it just handed over, so at the end the
parent holds nothing.

**G7. How does your loop avoid keeping every pipe alive at once?**

One variable, `prev_read`. Each iteration creates at most one new pipe, gives the
child the previous read end as stdin and the new write end as stdout, then closes
its own copies and keeps only the new read end for the next round.

**G8. Why does the child close `fd[0]` even though it only writes?**

Because it inherited both ends. Holding a read end it never uses would keep that
pipe open for other processes and can prevent a downstream EOF.

**G9. Can you implement a pipeline without `fork`?**

Not with `exec`-based commands, since `exec` consumes the process. You could
simulate it with threads or by running the stages sequentially through temporary
files, but you would lose the concurrency and the streaming.

**G10. What is the difference between an anonymous pipe and a FIFO?**

A FIFO (named pipe, created with `mkfifo`) has a filename, so unrelated
processes can open it. An anonymous pipe exists only as descriptors and must be
inherited.

**G11. Name the other IPC mechanisms and when you would use them.**

Signals (tiny notifications), pipes and FIFOs (byte streams between related or
co-located processes), message queues (discrete messages with priorities),
shared memory (fastest for bulk data, needs your own synchronisation), and
sockets (the only one that works across machines).

**G12. Which is fastest, and why is it not always the right answer?**

Shared memory, because after setup there are no kernel copies. But you then own
all the synchronisation — mutexes, semaphores, memory barriers — and getting that
wrong produces races that are far worse than the copy you saved.

**G13. Is a pipe full-duplex?**

Not on Linux; `pipe()` is one-way. Two-way communication needs two pipes, or a
`socketpair()`.

**G14. Why do the stages of a pipeline need to run concurrently?**

Because the buffer is finite. `cat big.txt | wc -l` would deadlock if `wc` were
started only after `cat` finished: `cat` would block at 64 KB with nobody
reading.

---

## Part H — Signals and the terminal

**H1. What is a signal?**

An asynchronous notification delivered to a process: a number, optionally with
context. A process can take the default action, ignore it, or catch it with a
handler. `SIGKILL` and `SIGSTOP` cannot be caught or ignored.

**H2. Why does your shell ignore SIGINT?**

So Ctrl-C kills the running command and not the shell. The terminal sends SIGINT
to every process in the foreground process group. Since my shell does not put
children in a separate group, both the shell and the child receive it — the shell
ignores it, and the child, which reset the disposition to default after `fork`,
dies.

**H3. Prove that works.**

In a pty session: `sleep 30`, then Ctrl-C, then `status` prints `130`
(128 + SIGINT). The prompt comes back and the shell is still usable.

**H4. What happens if you press Ctrl-Z in your shell?** **[trap]**

Both the shell and the command stop, and you land back in bash with
`[1]+ Stopped ./bin/mysh`. I verified it: both processes end up in state `T`.
That is because I never handle SIGTSTP and never put children in their own
process group, so the terminal stops the whole foreground group. Real job
control needs `setpgid()` per pipeline, `tcsetpgrp()` to hand over the terminal,
SIGTSTP/SIGTTOU handling and `waitpid(..., WUNTRACED)`. It is the top item on my
version 2 list.

**H5. What is a process group, and what is it for?**

A set of processes that receive terminal signals together, identified by a pgid.
It exists so that Ctrl-C can interrupt an entire pipeline rather than one stage.

**H6. What is a session and a controlling terminal?**

A session is a collection of process groups with one leader, usually the shell.
It may have a controlling terminal, which has exactly one foreground process
group; signals from the keyboard go to that group.

**H7. What happens if a background process reads from the terminal?**

It gets `SIGTTIN` and stops, because two processes competing for the keyboard
would be unusable. Writing can produce `SIGTTOU` depending on the terminal
settings.

**H8. Which keys generate which signals?**

Ctrl-C → SIGINT, Ctrl-\ → SIGQUIT (with a core dump), Ctrl-Z → SIGTSTP. Ctrl-D
is not a signal at all — it is end-of-file, which is why `getline` returns -1
and the shell exits.

**H9. Why is `SIG_IGN` inherited across `exec` but a handler is not?**

A handler is a function address in an address space that `exec` destroys, so it
cannot survive; "ignore" is just a flag in the kernel's table, so it can. That
asymmetry is exactly why the child must reset SIGINT explicitly.

**H10. What is async-signal-safety?**

A handler may interrupt the program anywhere, so it may only call functions that
are safe to re-enter — `write` is, `printf` and `malloc` are not. The usual
pattern is to set a `volatile sig_atomic_t` flag and do the work in the main
loop. Avoiding handlers altogether, as this shell does, avoids the problem.

**H11. What is a signal you cannot ignore, and why?**

`SIGKILL` and `SIGSTOP`, so that a process can always be killed or stopped by
the system regardless of what it wants.

**H12. You found that Ctrl-Z did nothing in your automated test. Why?**
**[trap]**

Because the test harness left the shell in an *orphaned process group* — no
member had a parent in a different process group of the same session. POSIX says
SIGTSTP, SIGTTIN and SIGTTOU sent to an orphaned process group are discarded,
precisely so a stopped process cannot be left with nobody able to continue it.
Once I ran the shell under a real bash in a pty, Ctrl-Z stopped it as expected.
The lesson is that a test environment can hide real behaviour.

**H13. How would you send a signal from the command line?**

`kill -SIGTERM <pid>`, or `kill -9` for SIGKILL. `kill` is a misleading name —
the system call sends any signal, not only fatal ones.

---

## Part I — Shell implementation deep dive

**I1. Walk me through what happens when I type `cat f.txt | wc -l > out.txt`.**

`getline` reads the line. `tokenize()` produces the words and marks `|` and `>`
as operators. `parse_line()` splits on `|` into two commands: `{cat, f.txt}` and
`{wc, -l}` with `outfile = "out.txt"`. Two stages, so no built-in lookup.
`run_pipeline()` creates one pipe, forks `cat` (stdout → pipe write end), forks
`wc` (stdin → pipe read end, stdout → the file via `open`+`dup2`), closes both
its own pipe ends, and waits for both children. `wc`'s exit code becomes
`last_status`.

**I2. Why must `cd` be a built-in?**

`fork()` gives the child a copy of the shell's state, including its current
directory. A child that calls `chdir()` moves itself and then exits, leaving the
shell where it started. The shell has to call `chdir()` in its own process.

**I3. Which of your commands are built-ins, and why each one?**

`cd` and `exit` change the shell's own state; `pwd` and `status` report it;
`help` documents it. All five are things an external program could not do for
the shell.

**I4. Why do you refuse built-ins inside a pipeline?**

A pipeline stage runs in a child, so `cd` there would be pointless and `exit`
would be wrong. Bash handles this by running built-ins in the subshell with
their normal semantics; I chose to reject it rather than half-implement it, and
the limitation is documented.

**I5. How is the exit status of a pipeline decided, and how does `status` see it?**

`run_pipeline()` waits for every child but only records the last one's code in
the global `last_status`, which the `status` built-in prints. That is bash's
`$?` behaviour: `false | true` gives 0.

**I6. Why is `last_status` a global? Would you change it?**

It is shell-wide state read by `status` and written by every execution path, so a
file-scope `static` is the honest representation in a single-file program. In a
larger program I would put it in a `struct shell` passed around, which also makes
it testable.

**I7. How does your shell handle a background command?**

A trailing `&` is stripped during parsing. `run_pipeline()` then skips the wait,
prints `[background] pid N`, and sets the status to 0. The child is reaped later
by `reap_background()` at a subsequent prompt.

**I8. Why does `reap_background()` only run when interactive?**

Because it is tied to printing the prompt. In a script there is no prompt, and
the shell exits at end of input; any remaining children are reparented to init,
which reaps them.

**I9. How do you detect that the shell is interactive?**

`isatty(STDIN_FILENO)`. When a script is piped in, there is no terminal, so the
prompt and the background reporting are skipped — which also keeps test output
clean.

**I10. What exit codes can your shell produce, and what does each mean?**

0 success, 1 a runtime failure (bad redirection target, `cd` failure), 2 a syntax
error, 126 found but not executable, 127 command not found, 128+N killed by
signal N, and whatever `exit N` was given.

**I11. What are the hard limits, and what happens when one is exceeded?**

128 tokens per line, 64 arguments per command, 16 pipeline stages, 256
characters per word. Each one is checked and produces a specific error message,
so a too-long line is a diagnostic, not a crash.

**I12. Is there a buffer overflow anywhere in your shell?**

Not that I have found. Every bounded copy is checked before the write —
`len == MAX_TOKEN - 1` in the tokenizer, `n == max` for tokens, `nwords ==
MAX_WORDS` for arguments — and the line itself is read with `getline`, which
grows its own buffer. I also ran everything under AddressSanitizer and
UndefinedBehaviorSanitizer with no reports.

**I13. Where does your shell allocate memory, and does it leak?**

Only `getline`'s buffer, which is reused across iterations and freed before
`main` returns. Everything else lives in fixed arrays. ASan's leak checker
reports nothing.

**I14. How would you add `&&` and `||`?**

Split the token list on those operators first, before splitting on `|`, giving a
list of pipelines with a connector between them. Then run left to right, checking
`last_status` after each: run the next one only if the status is 0 for `&&` or
non-zero for `||`. It is a small change because the status is already tracked.

**I15. How would you add `$VAR` expansion?**

A pass over each word after tokenizing: on `$`, read the name, look it up with
`getenv`, and splice in the value — with the rule that expansion happens in
unquoted and double-quoted text but not in single quotes. That is why the
tokenizer would need to remember *how* each part of a word was quoted, which is
currently thrown away.

**I16. Your shell has no `PATH` cache — is that a problem?**

`execvp` walks `PATH` on every command, which is a few `stat` calls. Bash keeps
a hash table (`hash -r` clears it) to avoid that. At interactive speeds the
difference is unmeasurable; it would matter in a loop running thousands of
commands.

---

## Part J — Parsing and the tokenizer

**J1. Why did you not use `strtok()`?**

Two reasons, both found by testing. It cannot see quotes, so
`tr ' ' '\n'` was split into four bogus words. And it silently skips empty
tokens, so `| wc -l` looked like a valid single command and actually ran `wc`,
which then consumed the rest of my script's input.

**J2. What does your tokenizer produce?**

An array of `struct token`, each holding the text and a flag saying whether it is
an operator. The flag is what distinguishes the operator `>` from the word `">"`
that the user quoted.

**J3. How do you handle quotes?**

On `'` or `"`, copy characters verbatim until the matching quote, then continue
the same word. So `--name="two words"` is one token, and reaching end-of-line
first is a syntax error: `unterminated " quote`.

**J4. What is the difference between single and double quotes in a real shell,
and in yours?**

In a real shell, double quotes still expand `$VAR` and backticks; single quotes
are fully literal. In mine there is no expansion at all yet, so the two behave
identically — an honest simplification I document rather than hide.

**J5. Why does `ls|wc -l` work without spaces?**

The word loop stops at any operator character, and the operator loop reads it as
its own token. Splitting on whitespace alone would require spaces around every
operator.

**J6. How do you tokenize `>>` without breaking `>`?**

Check the two-character operator first: on `>`, peek at the next character and
consume both if it is another `>`. Checking `>` first would turn `>>` into two
redirections and silently downgrade an append into a truncate.

**J7. How do you detect an empty pipeline stage?**

The pipe split records where each stage begins; if the stage's start index equals
its end index, there are no tokens between two `|`, or before the first one, and
that is `syntax error near '|'`. `strtok` could not express that, which is why
the bug existed.

**J8. Why is `&` only accepted as the last token?**

Because that is all my shell implements. `sleep 1 & echo hi` means two separate
commands in bash; rather than silently ignoring the `echo` or misrunning it, the
parser reports `syntax error near '&'`.

**J9. How does `#` work, and what is the limitation?**

At the start of a token, `#` ends the line — the rest is a comment. The
limitation is that it is not recognised mid-word, so `echo a#b` prints `a#b`,
same as bash.

**J10. This is a hand-written recursive-descent-ish parser. When would you use a
generator like lex/yacc?**

When the grammar grows: `&&`, `||`, `;`, subshells, `if`/`while`, here-documents.
For four operators and one nesting level, hand-written code is smaller, easier to
give good error messages from, and has no build dependency.

---

## Part K — Scheduling fundamentals

**K1. What is CPU scheduling?**

Deciding which ready process gets the CPU next, and for how long. The goal is to
balance utilisation, throughput, turnaround, waiting and response time — which
conflict, so every algorithm is a choice about what to sacrifice.

**K2. Name the three schedulers in an OS.**

Long-term (admission — which jobs enter the system), medium-term (swapping
processes in and out of memory), and short-term (which ready process runs next,
on a millisecond scale). This project simulates the short-term scheduler.

**K3. What is the difference between the scheduler and the dispatcher?**

The scheduler decides *who* runs; the dispatcher performs the switch — saving
and restoring register state, switching the address space, and jumping to user
mode. The time that takes is dispatch latency.

**K4. What is a context switch, and what does it cost?**

Saving one process's CPU state and restoring another's. The direct cost is
microseconds of register and page-table work; the indirect cost is usually
larger, because the new process starts with cold caches and TLB. This simulation
counts switches but models them as free, which is exactly why a 1-unit quantum
looks better here than it would on hardware.

**K5. Preemptive versus non-preemptive?**

Non-preemptive: a process keeps the CPU until it finishes or blocks (FCFS, SJF,
my Priority). Preemptive: the scheduler can take the CPU away (SRTF, RR, MLFQ).
Preemption buys responsiveness and fairness, and costs switches plus the need for
kernel synchronisation.

**K6. When does scheduling happen in a real kernel?**

When a process blocks or exits, when a timer interrupt ends a slice, when a
higher-priority process becomes ready, or when a process voluntarily yields.

**K7. Define turnaround, waiting and response time.**

Turnaround = finish − arrival (total time in the system). Waiting = turnaround −
burst (time in the system not running). Response = first time on the CPU −
arrival (how long before anything happens).

**K8. Why is `waiting = turnaround − burst` valid?**

Because every time unit between arrival and finish is either CPU time — exactly
`burst` of them — or waiting. There is nothing else to be doing on one CPU with
no I/O.

**K9. What is CPU utilisation, and why is it below 100% in one of your
workloads?**

Busy time ÷ total time. In `tests/workload3.txt`, X finishes at 2 and Y does not
arrive until 8, so the CPU is idle for six units and utilisation is 50%. No
scheduler can fix that — there is nothing to run.

**K10. What is throughput?**

Processes completed per unit time. In this project it is identical for every
algorithm on a given workload with no idle time, which is a useful thing to
notice: scheduling changes *who* waits, not the total work.

**K11. Why is the total time the same for all six algorithms?** **[trap]**

Because all six are work-conserving: the CPU is never left idle while something
is ready. The total CPU demand is fixed, so with no forced idleness the batch
must finish at the same instant. Anyone claiming their scheduler "finishes the
batch faster" on one CPU is mismeasuring.

**K12. What is starvation? How does it differ from deadlock?**

Starvation is indefinite postponement: a process is runnable but never chosen.
Deadlock is mutual blocking, where nobody can proceed at all. Starvation can
resolve by luck; deadlock cannot resolve itself. SJF and Priority can starve;
MLFQ's boost prevents it.

**K13. What is aging?**

Gradually improving the priority of a process that has been waiting a long time,
so a low-priority job eventually runs. It is the standard cure for starvation in
a priority scheduler, and MLFQ's periodic boost is the same idea on a fixed
schedule.

**K14. What is the convoy effect?**

Short processes stuck behind one long one under a non-preemptive scheduler. In
`tests/workload1.txt`, P4 needs 2 units of CPU and waits 18 under FCFS. It is
the motivation for every other algorithm here.

**K15. CPU-bound versus I/O-bound, and why does the mix matter?**

CPU-bound processes compute for long stretches; I/O-bound ones run briefly and
then block. Favouring I/O-bound processes keeps devices busy and makes
interactive use feel fast, which is why MLFQ promotes short-running processes.
My simulator has no I/O yet, so it can only approximate this by burst length.

**K16. How do real schedulers estimate an unknown burst length?**

Exponential averaging of past bursts: τ(n+1) = α·t(n) + (1−α)·τ(n). It is a
prediction, not knowledge, which is why MLFQ's "watch the behaviour" approach
tends to win in practice.

**K17. What does Linux actually use?**

Historically the O(1) scheduler, then the Completely Fair Scheduler (CFS), which
tracks per-task `vruntime` in a red-black tree and always runs the task with the
smallest virtual runtime, weighted by `nice`. Since kernel 6.6 the default is
EEVDF (Earliest Eligible Virtual Deadline First). Real-time policies
`SCHED_FIFO` and `SCHED_RR` sit above them, and both are closer to my Priority
and Round Robin than to MLFQ.

**K18. Is CFS an MLFQ?**

No — it has no discrete queues or demotion. It is a proportional-share scheduler:
instead of guessing who deserves priority, it gives everyone a fair fraction of
CPU time and runs whoever is furthest behind. The MLFQ *goal* (interactive
processes feel fast) is the same; the mechanism is different.

---

## Part L — FCFS

**L1. How does FCFS work?**

Sort by arrival, run each process to completion, and idle when nothing has
arrived. It is a queue at a shop counter and it is non-preemptive.

**L2. Its advantages?**

Trivial to implement, minimal overhead, the fewest context switches possible
(one per process), completely predictable, and it can never starve anyone.

**L3. Its disadvantage, with a number?**

The convoy effect. On workload 1, P4 (burst 2, arriving at 3) finishes at 23 with
a turnaround of 20 and a waiting time of 18. Under SJF the same process finishes
at 10.

**L4. Why are FCFS's average waiting and response times equal?** **[trap]**

Because it is non-preemptive: the moment a process first runs is the moment it
stops waiting, so response and waiting are the same number for every process
(8.75 on workload 1). Any scheduler where those two averages differ must be
preempting something.

**L5. Why is FCFS still worth studying?**

It is the baseline every other algorithm is measured against, it is what Round
Robin degenerates into with a large quantum, and it is what Priority becomes when
all priorities tie. Recognising those degenerate cases is a good correctness
check.

**L6. Why did you implement FCFS with a tick loop when `time += burst` would do?**

To make it structurally identical to the preemptive algorithms, so the six can be
read side by side, and because the loop is where idle time units get recorded for
the Gantt chart.

**L7. Why selection sort, and why the strict `<` comparison?**

Selection sort because 16 elements make speed irrelevant and it is much easier to
read than `qsort` with a comparator. The strict `<` keeps it stable for equal
arrival times, so processes that arrive together are served in file order — for a
tie, "first come" can only mean "first listed".

---

## Part M — SJF and SRTF

**M1. How does SJF choose?**

Among the processes that have arrived and are unfinished, the one with the
smallest total burst. Non-preemptive, so it then runs to completion.

**M2. What is SJF's big theoretical property?**

It gives the minimum possible average waiting time among non-preemptive
schedulers for a given set of arrived jobs.

**M3. Can you justify that?**

Exchange argument. Suppose a longer job runs before a shorter one. Swapping them
leaves every other job's finish time unchanged but reduces the short job's wait
by more than it increases the long job's, so total waiting falls. Any schedule
that is not shortest-first can be improved by such a swap, so the optimum is
shortest-first.

**M4. Why can no real OS use SJF?**

It needs the burst length before running the process, which is unknowable in
general. Real kernels either predict it (exponential averaging) or sidestep it
(MLFQ observes behaviour instead).

**M5. What is SJF's failure mode?**

Starvation of long jobs: if short jobs keep arriving, a long one may never be
chosen. There is no aging in plain SJF.

**M6. How does SRTF differ?**

It is preemptive and compares *remaining* time, re-deciding at every time unit.
A short arrival immediately displaces a long running process.

**M7. Show the difference with numbers.**

Workload 1: SJF gives 12.25 turnaround / 6.50 waiting / 6.50 response with 3
switches; SRTF gives 10.75 / 5.00 / 3.50 with 4 switches. One extra switch buys
1.5 units of average waiting time.

**M8. Is SRTF optimal?**

Yes, for average waiting time on a single CPU with known burst lengths —
including arrivals over time, which is what SJF alone cannot handle. That is why
I use it as the yardstick in `compare`: no algorithm should ever beat its waiting
time, and if one did, I would suspect a bug in my simulator.

**M9. Explain the tie-break in `pick_shortest_remaining()`.** **[trap]**

The currently running process is seeded as the initial best candidate and the
comparison is strictly `<`, so a challenger must be *strictly* shorter to take
the CPU. Without that, two processes with equal remaining time would swap every
single time unit, inflating the context-switch count with no benefit — a real bug
that produces correct averages and a nonsense Gantt chart.

**M10. Why does SRTF set `start_time` only if it is negative?**

Because a preempted process gets the CPU many times, and response time is
measured from the first of them. Writing it every time would make response time
equal to the last dispatch instead of the first.

**M11. On workload 2, SRTF's turnaround is 6.80 against FCFS's 11.20. Why such a
big gap?**

Workload 2 has a 20-unit process running when two short processes arrive at t=12
and 13. FCFS makes them wait for all 20 units; SRTF interrupts immediately
because their remaining time is far smaller. The bigger the burst spread, the
bigger the gap.

**M12. Where would you actually use SJF-like scheduling?**

Where burst lengths genuinely are known or well-estimated: batch and print
queues, some database query schedulers, CI job runners that know historical job
durations. Not in a general-purpose kernel.

---

## Part N — Round Robin

**N1. How does Round Robin work?**

A FIFO ready queue and a fixed quantum. The process at the front runs for up to
one quantum; if it is not finished it goes to the back and the next one runs.

**N2. What is Round Robin good at?**

Response time and fairness, without knowing anything about the future. Every
process gets the CPU within (n−1) quanta, which is a real guarantee.

**N3. What happens as the quantum grows?**

It becomes FCFS. On workload 1 with `-q 20` — larger than every burst — the
numbers are exactly FCFS's: 14.50 / 8.75 / 8.75 with 3 switches. The test suite
asserts this.

**N4. What happens as the quantum shrinks?**

Response time approaches its best (0.75 with `-q 1` on workload 1) but switches
explode (20 instead of 3). In the limit it is processor sharing, which is ideal
in a model where switching is free and terrible on real hardware.

**N5. How should a real quantum be chosen?**

Large compared with the context-switch cost (so overhead stays a small
percentage), small compared with typical bursts (so responsiveness survives).
The usual rule of thumb is that ~80% of bursts should be shorter than the
quantum. Linux's CFS effectively computes a dynamic slice rather than using a
fixed one.

**N6. Why is Round Robin's average waiting time on workload 1 (10.00) worse than
FCFS's (8.75)?** **[trap]**

Because Round Robin is not trying to minimise waiting. Slicing spreads every
process's completion later while letting each start sooner, so response time
improves (3.00 vs 8.75) and waiting gets worse. This is the clearest example in
the project that "better scheduler" is meaningless without naming the metric.

**N7. In your implementation, why are arrivals admitted before the quantum-expiry
check?** **[trap]**

Because a process arriving at exactly the instant a slice expires should queue
*ahead* of the preempted process, which is the usual convention. Reversing the
two lines changes the Gantt chart — it is a real decision, not an accident, and
it is the kind of detail two textbook solutions can legitimately disagree on.

**N8. What data structure does your ready queue use?**

A circular buffer: `items[]` with a `head` index and a `count`, where a push goes
to `(head + count) % MAX_PROCS`. The modulo makes it wrap, so unlimited pushes
and pops never run off the end.

**N9. Why not a linked list?**

A fixed-capacity ring needs no allocation, has no pointer-chasing, and cannot
leak. The maximum occupancy is known — a process is in the queue at most once —
so the array can never overflow.

**N10. Is Round Robin used in practice?**

Yes: `SCHED_RR` in Linux is real-time round robin within a priority level, and
round robin is the mechanism *inside* each queue of an MLFQ. It is rarely the
whole scheduler on its own.

---

## Part O — Priority scheduling

**O1. How does your priority scheduler work?**

Among arrived, unfinished processes it picks the smallest priority number
(1 beats 3), then runs it to completion. The priority comes from an optional
fourth column in the workload file.

**O2. On `tests/workload4.txt`, LOW (priority 3) runs first. Is that a bug?**
**[trap]**

No — it is the definition of non-preemptive. At time 0, LOW is the only process
that has arrived, so it gets the CPU, and the arrival of HIGH (priority 1) at
t=2 cannot take it away. A preemptive priority scheduler would switch at t=2.
This is the question most people get wrong about their own output.

**O3. What is the danger of priority scheduling?**

Starvation of low-priority work. In workload 4, BG (priority 4) runs last, and if
important work kept arriving it would never run at all.

**O4. How do you fix that?**

Aging: raise the priority of anything that has waited too long. The alternative
seen in real systems is to reserve a share of CPU for low-priority classes.

**O5. What happens on a workload with no priority column?**

Every process gets priority 0, they all tie, and the arrival-order tie-break
makes it behave exactly like FCFS — 14.50 / 8.75 on workload 1. The test suite
asserts that, because it proves the tie-breaking works.

**O6. Static versus dynamic priority?**

Static is fixed at creation (my implementation, and `SCHED_FIFO` in Linux).
Dynamic changes as the process runs — through aging, or by promoting I/O-bound
processes. MLFQ is a dynamic-priority scheduler.

**O7. What is priority inversion?**

A high-priority task blocked on a lock held by a low-priority task, which itself
cannot run because a medium-priority task is using the CPU. The classic fixes are
priority inheritance and priority ceilings. It famously affected the Mars
Pathfinder. My simulator has no locks, so it cannot exhibit it.

**O8. How does `nice` relate to this?**

`nice` adjusts a process's scheduling weight in Linux (−20 most favourable, +19
least). Under CFS it scales `vruntime` accumulation rather than acting as a strict
priority, so a `nice +19` process still gets some CPU — unlike my scheduler,
where a lower-priority process can be starved indefinitely.

---

## Part P — MLFQ

**P1. What problem does MLFQ solve?**

Getting SJF-like responsiveness without knowing burst lengths in advance. It
learns from behaviour: a process that keeps using its whole slice is treated as a
long job.

**P2. State the rules you implemented.**

R1: new processes enter the highest queue Q0. R2: always run something from the
highest non-empty queue, FIFO within a queue. R3: a process that uses its whole
slice is demoted one level. R4: a process that arrives in a higher queue preempts
the running one. R5: every 15 time units everything is boosted back to Q0.

**P3. What is your configuration?**

Three queues with slices 2, 4 and 8, and a boost interval of 15 — all compile-time
constants in `src/algo_mlfq.c`.

**P4. Why does a lower-priority queue get a longer slice?**

Processes there are long-running, so longer slices mean fewer switches per unit of
useful work. Short interactive jobs never reach that queue, so they are not
penalised by it.

**P5. Why is the priority boost necessary?**

Without it, a long job demoted to Q2 can starve while short jobs keep arriving in
Q0. The boost is a guarantee of eventual progress. On workload 1 you can see it:
at t=15 the boost returns P1 (in Q2) and the running P3 to Q0, and P1 finishes at
19 instead of being stuck.

**P6. Why is the running process boosted separately in your code?** **[trap]**

Because it has already been popped out of its queue — the boost loop drains
`queues[1..2]`, and the process holding the CPU is in none of them. Missing that
is a silent bug that would leave the running process demoted forever.

**P7. Why does R4 only check queues strictly above the running one?**

So a process arriving at the *same* priority waits its turn instead of
interrupting. Preempting on an equal-priority arrival would thrash.

**P8. Why is completion checked before demotion?**

A process that finishes exactly at the end of its slice should just finish; the
alternative pointlessly demotes it and clutters the event log. Order of checks in
a tick loop is behaviour, not style.

**P9. Why one tick at a time instead of running a whole slice?**

Because arrivals happen mid-slice. A tick loop notices a Q0 arrival immediately,
which is what makes R4 possible. Running whole slices would silently delay every
preemption.

**P10. How can a process game MLFQ, and how do real systems stop it?**

By voluntarily yielding just before its slice expires — it never "uses" a whole
slice, so it is never demoted, and it keeps top priority forever. The fix is
better accounting: track the total CPU a process has used *at* a level rather
than per slice, and demote when the cumulative allowance is exhausted. My
simulator has no yield, so it cannot be gamed, but the accounting change would
be the right fix.

**P11. What do the numbers show for MLFQ on workload 1?**

14.50 turnaround / 8.75 waiting / 1.50 response with 8 switches. Turnaround and
waiting match FCFS exactly, and response time is nearly 6× better — MLFQ moved
the waiting from short jobs onto the long one rather than removing it.

**P12. Trace the first eight time units on workload 1.**

t=0 P1 arrives in Q0 and runs. t=1 P2 arrives. t=2 P1 has used its 2-unit slice
and is demoted to Q1; P3 arrives; P2 runs. t=3 P4 arrives. t=4 P2 is demoted to
Q1; P3 runs. t=6 P3 is demoted to Q1; P4 runs. t=8 P4 finishes — still in Q0,
because a 2-unit job fits inside its first slice. That is the whole point: the
short job never gets demoted and its response time is 3 instead of FCFS's 18.

**P13. How would you make the configuration runtime-adjustable?**

Replace the constants with a small config struct filled from command-line options
(`--queues`, `--quantum`, `--boost`), passed into `run_mlfq()`. The algorithm code
would barely change; it already indexes the quantum by queue level.

**P14. What is the difference between a multilevel queue and a multilevel
*feedback* queue?**

In a plain multilevel queue a process is assigned to one queue permanently. The
feedback version moves processes between queues based on observed behaviour —
demotion on slice exhaustion, promotion on boost. The feedback is the entire
innovation.

---

## Part Q — Numerical problems with full solutions

Every answer below was verified by running the simulator, so you can check your
hand working against it.

### Q1. FCFS, SJF, SRTF and RR(q=2) on the same workload

| process | arrival | burst |
| ------- | ------- | ----- |
| P1 | 0 | 5 |
| P2 | 1 | 3 |
| P3 | 2 | 8 |
| P4 | 3 | 6 |

**FCFS.** Order P1 P2 P3 P4.

```
| P1 | P2 |  P3  |  P4  |
0    5    8      16     22
```

| process | start | finish | turnaround | waiting | response |
| ------- | ----- | ------ | ---------- | ------- | -------- |
| P1 | 0 | 5 | 5 | 0 | 0 |
| P2 | 5 | 8 | 7 | 4 | 4 |
| P3 | 8 | 16 | 14 | 6 | 6 |
| P4 | 16 | 22 | 19 | 13 | 13 |

Averages: turnaround 45/4 = **11.25**, waiting 23/4 = **5.75**, response
**5.75**, context switches 3.

**SJF.** At t=5 the ready set is P2(3), P3(8), P4(6) → P2. At t=8 it is P3(8) vs
P4(6) → P4. Order P1 P2 P4 P3.

| process | start | finish | turnaround | waiting |
| ------- | ----- | ------ | ---------- | ------- |
| P1 | 0 | 5 | 5 | 0 |
| P2 | 5 | 8 | 7 | 4 |
| P3 | 14 | 22 | 20 | 12 |
| P4 | 8 | 14 | 11 | 5 |

Averages: turnaround **10.75**, waiting **5.25**, response **5.25**, switches 3.

**SRTF.** t=0 P1 runs. t=1 P2 arrives with 3 < P1's remaining 4 → preempt. P2
finishes at 4. Then P1 (remaining 4) beats P4(6) and P3(8), running 4–8. Then P4,
then P3.

```
| P1 | P2  |  P1  |  P4  |  P3  |
0    1     4      8      14     22
```

| process | start | finish | turnaround | waiting | response |
| ------- | ----- | ------ | ---------- | ------- | -------- |
| P1 | 0 | 8 | 8 | 3 | 0 |
| P2 | 1 | 4 | 3 | 0 | 0 |
| P3 | 14 | 22 | 20 | 12 | 12 |
| P4 | 8 | 14 | 11 | 5 | 5 |

Averages: turnaround **10.50**, waiting **5.00**, response **4.25**, switches 4.
This beats SJF on all three time metrics for the price of one extra context
switch — that is preemption paying off.

**Round Robin, quantum 2.**

```
|P1|P2|P3|P1|P4|P2|P3|P1|P4|P3|P4|P3|
0  2  4  6  8  10 11 13 14 16 18 20 22
```

| process | start | finish | turnaround | waiting | response |
| ------- | ----- | ------ | ---------- | ------- | -------- |
| P1 | 0 | 14 | 14 | 9 | 0 |
| P2 | 2 | 11 | 10 | 7 | 1 |
| P3 | 4 | 22 | 20 | 12 | 2 |
| P4 | 8 | 20 | 17 | 11 | 5 |

Averages: turnaround **15.25**, waiting **9.75**, response **2.00**, switches 11.
Worst turnaround of the four, best response — the trade-off in one line.

Verify with:

```bash
printf 'P1 0 5\nP2 1 3\nP3 2 8\nP4 3 6\n' > /tmp/q1.txt
./bin/sched compare -q 2 /tmp/q1.txt
```

### Q2. Non-preemptive priority (smaller number = higher priority)

| process | arrival | burst | priority |
| ------- | ------- | ----- | -------- |
| J1 | 0 | 4 | 2 |
| J2 | 1 | 3 | 1 |
| J3 | 2 | 5 | 3 |
| J4 | 3 | 2 | 1 |

At t=0 only J1 has arrived, and it cannot be preempted, so it runs 0–4. At t=4
the ready set is J2(p1), J3(p3), J4(p1); J2 and J4 tie at priority 1, and the
tie-break is earlier arrival → J2. Then J4, then J3.

```
| J1 | J2 | J4 |  J3  |
0    4    7    9      14
```

| process | start | finish | turnaround | waiting |
| ------- | ----- | ------ | ---------- | ------- |
| J1 | 0 | 4 | 4 | 0 |
| J2 | 4 | 7 | 6 | 3 |
| J3 | 9 | 14 | 12 | 7 |
| J4 | 7 | 9 | 6 | 4 |

Averages: turnaround **7.00**, waiting **3.50**, response **3.50**.

Follow-up: what changes if the scheduler is *preemptive*? J2 takes the CPU at
t=1 (priority 1 vs J1's 2), and J1 is pushed back — so the answer changes
completely. Say which variant you are computing before you start.

### Q3. A workload with an idle CPU

| process | arrival | burst |
| ------- | ------- | ----- |
| A | 0 | 3 |
| B | 5 | 2 |
| C | 6 | 4 |

FCFS:

```
| A | idle | B |  C  |
0   3      5   7     11
```

| process | start | finish | turnaround | waiting |
| ------- | ----- | ------ | ---------- | ------- |
| A | 0 | 3 | 3 | 0 |
| B | 5 | 7 | 2 | 0 |
| C | 7 | 11 | 5 | 1 |

Averages: turnaround 10/3 = **3.33**, waiting 1/3 = **0.33**. Busy 9 of 11 time
units, so CPU utilisation is **81.8%** and throughput is 3/11 = **0.273**.

**Why do all six algorithms give identical numbers here?** Because there is
almost no contention: at most one process is ready at any moment except for the
single unit where B and C overlap. With nothing to choose between, every policy
makes the same choice. Good scheduling questions always involve overlap.

### Q4. MLFQ trace

Using `tests/workload1.txt` — P1(0,8), P2(1,4), P3(2,9), P4(3,2) — with three
queues of slices 2/4/8 and a boost every 15 units, list the demotions.

Answer: P1 demoted to Q1 at t=2, P2 to Q1 at t=4, P3 to Q1 at t=6, P4 finishes at
t=8 while still in Q0, P1 demoted to Q2 at t=12, P2 finishes at t=14, boost at
t=15 returns P1 and P3 to Q0, P3 demoted to Q1 at t=17, P1 finishes at t=19, P3
at t=23. Final averages: 14.50 / 8.75 / 1.50 with 8 switches.

```bash
./bin/sched mlfq tests/workload1.txt      # prints exactly this event log
```

### Q5. Reasoning without arithmetic

**Given a workload where every process arrives at time 0 with equal bursts, which
algorithm is best?** All of them are identical, apart from Round Robin with a
small quantum, which is strictly worse — it adds context switches and delays
every completion without helping anyone. Equal-length jobs are the worst case for
slicing.

**If you double every burst, what happens to average waiting time?** It roughly
doubles (exactly doubles if all arrivals are at 0, since the whole schedule
scales). Waiting time is not scale-invariant, so quoting it without the workload
is meaningless — which is why the README always names the workload file.

---

## Part R — Data structures and algorithms used

**R1. What data structures does the project use?**

Fixed arrays of structs for processes, tokens, commands and CPU slices; a
circular-buffer FIFO queue for Round Robin and for each MLFQ level; and
NULL-terminated string arrays for `argv`.

**R2. Explain the circular queue.**

`items[MAX_PROCS]`, plus `head` (where the next pop comes from) and `count` (how
many are stored). Push writes at `(head + count) % MAX_PROCS`; pop reads at
`head` and advances it modulo the size. It is O(1) for both operations with no
allocation.

**R3. Why is `MAX_PROCS` slots always enough?**

A process can be in at most one queue at a time, so the total across all queues
never exceeds the number of processes.

**R4. What would break if you used `head`/`tail` indices without a count?**

Full and empty become indistinguishable — both give `head == tail`. The usual
fixes are to keep a count (what I did) or to waste one slot.

**R5. What is the complexity of each algorithm as implemented?**

FCFS is O(n²) for the selection sort plus O(total burst) for the simulation.
SJF and Priority scan all processes per decision: O(n) per pick. SRTF scans per
tick: O(n · total burst). RR and MLFQ are O(1) per tick thanks to the queues.
With n ≤ 16 all of this is irrelevant, but it is worth knowing which would matter
at scale.

**R6. How would you make SJF and SRTF faster for large n?**

A min-heap (priority queue) keyed on burst or remaining time gives O(log n) per
decision instead of O(n). For SRTF you also need to update a key when the running
process's remaining time changes, so an indexed heap or a balanced tree — which
is essentially what CFS does with a red-black tree on `vruntime`.

**R7. How are the Gantt segments stored?**

`struct segment {int proc; int start; int end;}` in an array, where `proc == -1`
means idle. `add_tick()` extends the last segment when the same process runs
again in the next time unit, so 8 consecutive ticks become one segment — that
merging is what makes the chart readable.

**R8. Why record the timeline as slices rather than a per-tick array?**

Slices are compact and are exactly what the Gantt chart needs. The per-process
timeline view is derived from them by asking "who ran at time t", so there is one
source of truth instead of two that could disagree.

**R9. Where is the sorting, and is it stable?**

Only in FCFS, and yes — the comparison is strict `<`, so equal arrival times keep
their original order. Stability is a correctness requirement here, not an
implementation detail.

---

## Part S — Testing, debugging and tooling

**S1. How do you test the project?**

`make test` runs 87 checks in a bash script: shell behaviour (commands, quoting,
pipes, redirection, error codes, built-ins, background jobs) and simulator
behaviour (each algorithm's metrics and ordering, quantum extremes, compare mode,
CSV output, command-line and workload validation). It exits non-zero if anything
fails, so it can drop into CI.

**S2. How do you know the *expected* values are right?** **[trap]**

I computed them by hand before running the code. A test that records whatever the
program printed will happily lock in a bug forever. Two of the checks are
property-based instead of value-based: RR with a huge quantum must equal FCFS,
and Priority with no priorities must equal FCFS.

**S3. Give an example of a test that failed for the wrong reason.**

One check asserted that every algorithm reports the same total time by pulling the
last column out of the `compare` table with `awk '/^  (FCFS|SJF|...)/'`. The
closing note under the table begins "SRTF is the theoretical best...", so it
matched too and contributed the word `it`. I changed the pattern to match the
shape of a data row (`$2` looks like `14.50`) instead of its first word.

**S4. How do you test something interactive like Ctrl-C?**

Through a pseudo-terminal, with Python's `pty.fork()`: exec the shell on the
slave side, write `sleep 30\n`, then the raw byte `\x03`, then `status\n` and
check it prints 130. A pipe cannot be used, because the shell checks `isatty()`
and because signals come from the terminal driver.

**S5. What did you learn from that harness?**

That a test environment can hide real behaviour. Ctrl-Z appeared to do nothing
because the harness left the shell in an orphaned process group, and POSIX says
SIGTSTP to an orphaned group is discarded. Running the shell under a real bash in
a pty showed the true behaviour: both processes stop.

**S6. How did you check for memory errors without valgrind?**

Compiled everything with `-fsanitize=address,undefined` and ran the whole matrix
— every algorithm against every workload, the quantum extremes, the CSV paths and
the error paths, plus a shell script exercising pipes, redirection and syntax
errors. No reports, no leaks.

**S7. What is the difference between ASan and valgrind?**

ASan is a compile-time instrumentation: much faster, catches out-of-bounds and
use-after-free, needs a rebuild. Valgrind runs the unmodified binary in a virtual
CPU: slower, but also finds uninitialised reads and works on binaries you cannot
recompile.

**S8. How would you debug a hanging pipeline?**

`ps` to see which stage is alive, then `strace -p <pid>` — a process blocked in
`read()` on a pipe with no writer is the signature. `ls -l /proc/<pid>/fd` shows
which pipes it still holds, which is how you find the descriptor somebody forgot
to close.

**S9. How would you debug a crash?**

Build with `-g` (already the default here), run under `gdb`, `bt` for the
backtrace, then inspect the frame. For a memory bug ASan usually names the exact
line faster than gdb does.

**S10. What is in your `.gitignore`, and why?**

`bin/` and `*.o` — build outputs that `make` recreates. Committing binaries makes
diffs useless and the repository large.

**S11. How would you set up CI for this?**

A GitHub Actions job that runs `make` and `make test` on push, plus a second job
building with the sanitizers and running the same suite. The script already
returns a non-zero exit code on failure, so nothing else is needed.

**S12. What would you add to the test suite next?**

A test that runs each algorithm against a randomly generated workload and checks
invariants rather than values: every process finishes, total busy time equals the
sum of bursts, no two processes run in the same time unit, and no algorithm beats
SRTF's average waiting time. Property-based tests find the cases I would never
think to write by hand.

---

## Part T — Design and code quality

**T1. Why one `sched` binary instead of six programs?**

Everything except the choice of algorithm is identical — loading the workload,
printing the chart, computing metrics. Six programs meant six copies of `main()`,
and the comparison mode would have been impossible. Now each algorithm is one
function and adding a seventh is three small edits.

**T2. Why is each algorithm in its own file?**

So they can be read side by side, which is the whole educational point. Each file
is 60–180 lines containing one scheduling decision and nothing else.

**T3. What is the contract every algorithm obeys?**

Take the process array, fill in `start_time` and `finish_time` for each process,
call `add_tick()` for every time unit including idle ones, and return the number
of slices recorded. Nothing else. That uniformity is what lets `compare` treat
them interchangeably.

**T4. Why an if/else chain in `run_algorithm()` instead of function pointers?**

Because the six functions do not take the same arguments — Round Robin needs a
quantum, MLFQ needs a verbosity flag. A table of function pointers would need a
lowest-common-denominator signature that hides those differences, and hiding a
real difference makes code harder to read, not easier.

**T5. Why does `compare` copy the workload for each algorithm?**

Because the algorithms destroy it: `remaining` counts to zero, `start_time` and
`finish_time` are overwritten, and FCFS sorts the array. Sharing one array would
make every algorithm after the first "finish" instantly and print plausible
nonsense — the worst kind of bug, because the output still looks reasonable.

**T6. How do you know the copy is actually working?**

There is a test asserting that all six algorithms report the same total time. If
the copy broke, five of them would report a wildly different number and the test
would fail immediately.

**T7. Why is the shell one big file when the simulator is split?**

The shell is one pipeline of stages that all share the same small state; splitting
it would mean exporting internals through headers for no benefit. The simulator
has six genuinely independent implementations of one interface, which is exactly
what separate files are for. Structure should follow the shape of the problem.

**T8. Your `compute_metrics()` returns a struct by value. Why not a pointer?**

The struct is small and the function is called once per run, so a copy costs
nothing and returning by value means no ownership question and no possibility of
a NULL pointer.

**T9. Why do error messages go to stderr rather than stdout?**

So redirection still works: `./bin/sched fcfs bad.txt > out.txt` should put the
error on the terminal, not silently into the file. It is also why the shell's
diagnostics do not interfere with `cmd | grep`.

**T10. What would a reviewer criticise?**

Fair criticisms: `last_status` is a file-scope global rather than shell state
passed around; the shell's `execute_line()` does parsing and dispatch in one
function; there are no unit tests at function level, only end-to-end tests; and
the MLFQ configuration is compile-time. I would fix the global first, since it is
the one that would actually block adding features like `$?`.

---

## Part U — Spot the bug

**U1. What is wrong here?**

```c
pid_t pid = fork();
if (pid == 0) {
    dup2(fd[1], STDOUT_FILENO);
    execvp(argv[0], argv);
}
close(fd[1]);
wait(NULL);
```

Two bugs. The child never closes `fd[0]`, and — worse — there is no error
handling after `execvp`, so if it fails the child falls through and *becomes a
second copy of the parent*, running the rest of the parent's code. Every child
must `_exit()` if `exec` fails.

**U2. Why does this hang?**

```c
pipe(fd);
if (fork() == 0) { dup2(fd[1], 1); close(fd[1]); execvp("echo", ...); }
if (fork() == 0) { dup2(fd[0], 0); close(fd[0]); execvp("wc", ...); }
wait(NULL); wait(NULL);
```

The parent never closes either end. `wc` waits for end-of-file, which requires
every copy of the write end to be closed, and the parent still holds one — so
`wc` never finishes and the parent never returns from `wait`. This is the exact
deadlock I reproduced on purpose with exit code 124.

**U3. What is wrong with this redirection?**

```c
int fd = open("out.txt", O_WRONLY | O_CREAT);
dup2(fd, STDOUT_FILENO);
```

Two problems. `O_CREAT` without a mode argument gives the file whatever garbage
is in the third variadic slot. And there is no `O_TRUNC` or `O_APPEND`, so
writing to an existing longer file overwrites the front and leaves the old tail
behind. Also `fd` should be closed after the `dup2`, and both calls need error
checks.

**U4. Why might this print twice?**

```c
printf("starting\n");
if (fork() == 0) { do_child_work(); exit(0); }
```

If stdout is a pipe or a file it is fully buffered, so `"starting\n"` may still
be in the buffer at `fork()`. The child inherits a copy, and its `exit(0)`
flushes it — so the line appears twice. Fix: `fflush(stdout)` before forking, and
`_exit()` in the child.

**U5. What is wrong with this MLFQ boost?**

```c
for (int q = 1; q < NQUEUES; q++)
    while (!q_empty(&queues[q])) {
        int i = q_pop(&queues[q]);
        procs[i].queue = 0;
        q_push(&queues[0], i);
    }
```

It misses the currently running process, which was popped out of its queue and is
therefore in none of them. It stays demoted forever, exactly the process the boost
was meant to rescue. My code handles it with a separate check on `running`.

**U6. What is wrong with this tie-break in SRTF?**

```c
for (int i = 0; i < n; i++)
    if (ready(i) && (best < 0 || procs[i].remaining <= procs[best].remaining))
        best = i;
```

`<=` instead of `<`. With equal remaining times the choice flips every tick, so
the CPU ping-pongs between two processes and the context-switch count explodes,
while the averages stay correct and hide the problem. Preferring the incumbent on
a tie is the fix.

---

## Part V — Extend the project

**V1. How would you add I/O bursts to the simulator?**

Give each process a list of alternating CPU and I/O bursts. Add a blocked state
and an I/O completion time; when a CPU burst ends, move the process to blocked
until its I/O finishes, then back to the ready queue. Metrics gain "time
blocked", and CPU utilisation becomes interesting because the CPU can now idle
while work exists. In MLFQ, a process that blocks before using its slice should
*keep* its priority — that is how the algorithm detects interactivity.

**V2. How would you extend it to multiple CPUs?**

Replace the single `running` variable with an array of per-CPU running processes,
and decide between one shared ready queue (simple, but contended and cache-hostile)
and per-CPU queues with load balancing (what Linux does). New questions appear
immediately: processor affinity, migration cost, and whether idle CPUs steal work.

**V3. How would you add aging to the priority scheduler?**

Every k time units, decrement the effective priority number of every waiting
process (bounded), and reset it when the process runs. Keep the file's priority
separate from the effective one so the workload stays declarative.

**V4. How would you implement real job control in the shell?**

`setpgid()` in both parent and child so each pipeline gets its own process group;
`tcsetpgrp()` to give that group the terminal for foreground jobs; ignore
SIGTTOU in the shell to avoid being stopped when it takes the terminal back;
`waitpid(..., WUNTRACED)` to notice stopped children; a job table; and the
`jobs`, `fg` and `bg` built-ins. Ctrl-Z then stops the *job*, not my shell.

**V5. How would you add `&&`, `||` and `;`?**

Split the token list on those operators before splitting on `|`, producing a list
of pipelines with connectors. Execute left to right, consulting `last_status` to
decide whether to run the next. The status plumbing already exists.

**V6. How would you make the shell support globbing (`*.c`)?**

After tokenizing, expand any unquoted word containing `*`, `?` or `[` with
`glob(3)`, replacing it with the matches (and leaving it literal if there are
none, like bash). The catch is that quoting information must survive tokenizing,
so `"*.c"` is not expanded — which is why a real shell tracks quoting per
character.

**V7. Someone asks for a GUI. What would you do?**

Keep the C engine and add a `--csv` consumer, which already exists. The cheapest
credible option is a small HTML page with JavaScript reading the CSV and drawing
an animated Gantt chart; the cheapest of all is plotting the CSV with gnuplot or
a spreadsheet. Rewriting the engine in another language would risk the
correctness I have already verified.

**V8. How would you profile this if it were slow?**

`perf stat` for a summary and `perf record`/`report` for hotspots, or `gprof`
with `-pg`. For this workload size it is not worth it: the whole simulation is
microseconds, and the honest answer is that measurement should come before
optimisation.

**V9. What is the single most valuable next commit?**

Job control in the shell, because it is the biggest gap between "toy" and
"usable" and it forces me to learn process groups and terminal ownership
properly. For the simulator it would be I/O bursts, because without blocking,
MLFQ cannot demonstrate the interactive behaviour it exists to reward.

---

## Part W — Rapid fire

| # | question | answer |
| - | -------- | ------ |
| 1 | `fork()` return in child? | 0 |
| 2 | `fork()` return in parent? | child's pid |
| 3 | `fork()` on failure? | −1 |
| 4 | Does `exec` change the pid? | No |
| 5 | What does `execvp`'s `p` mean? | searches `PATH` |
| 6 | The real exec system call? | `execve` |
| 7 | Reap a child with? | `wait` / `waitpid` |
| 8 | Zombie? | terminated, not yet reaped |
| 9 | Orphan? | parent died; reparented to PID 1 |
| 10 | Kill a zombie with `kill -9`? | No, it is already dead |
| 11 | `WNOHANG` means? | do not block if no child has finished |
| 12 | Exit code for command not found? | 127 |
| 13 | Exit code for not executable? | 126 |
| 14 | Exit code for syntax error (bash)? | 2 |
| 15 | Exit code after Ctrl-C? | 130 (128 + 2) |
| 16 | Signal number of SIGINT? | 2 |
| 17 | Signal number of SIGKILL? | 9 |
| 18 | Signals you cannot catch? | SIGKILL, SIGSTOP |
| 19 | Ctrl-C sends? | SIGINT |
| 20 | Ctrl-Z sends? | SIGTSTP |
| 21 | Ctrl-D sends? | nothing — it is end-of-file |
| 22 | Ctrl-\ sends? | SIGQUIT |
| 23 | fd 0, 1, 2? | stdin, stdout, stderr |
| 24 | `dup2(a, b)` does? | makes `b` refer to `a`'s open file |
| 25 | `>` uses which flag? | `O_TRUNC` |
| 26 | `>>` uses which flag? | `O_APPEND` |
| 27 | `<` uses which flag? | `O_RDONLY` |
| 28 | Default file mode passed for `O_CREAT` here? | 0644 |
| 29 | Does `umask` affect it? | Yes: `0644 & ~umask` |
| 30 | `pipe(fd)`: which end reads? | `fd[0]` |
| 31 | Pipe capacity on Linux? | 64 KB by default |
| 32 | Reader sees EOF when? | all write ends are closed |
| 33 | Write to a pipe with no reader? | SIGPIPE, or `EPIPE` |
| 34 | Is a pipe bidirectional? | No |
| 35 | Named pipe is created with? | `mkfifo` |
| 36 | Fastest IPC? | shared memory |
| 37 | Pipeline exit status is? | the last command's |
| 38 | Why must `cd` be a built-in? | `chdir` in a child dies with the child |
| 39 | Built-ins in this shell? | cd, pwd, status, help, exit |
| 40 | Max pipeline stages here? | 16 |
| 41 | Why `_exit` in a child? | avoids flushing the parent's buffers twice |
| 42 | Why `fflush` before `fork`? | the child inherits the unflushed buffer |
| 43 | `setvbuf(_IOLBF)` means? | flush at every newline |
| 44 | Why `_POSIX_C_SOURCE`? | to expose `getline` under `-std=c11` |
| 45 | Turnaround time? | finish − arrival |
| 46 | Waiting time? | turnaround − burst |
| 47 | Response time? | first run − arrival |
| 48 | CPU utilisation? | busy ÷ total time |
| 49 | Throughput? | processes ÷ total time |
| 50 | Which algorithms here are preemptive? | SRTF, RR, MLFQ |
| 51 | Which are non-preemptive? | FCFS, SJF, Priority |
| 52 | Optimal average waiting time? | SRTF |
| 53 | Best response time on workload 1? | MLFQ (1.50) |
| 54 | Fewest context switches? | FCFS (3) |
| 55 | Convoy effect? | short jobs stuck behind a long one |
| 56 | Starvation? | runnable but never scheduled |
| 57 | Cure for starvation? | aging, or a periodic boost |
| 58 | RR with a huge quantum becomes? | FCFS |
| 59 | RR with quantum 1 costs? | 20 context switches here |
| 60 | Default RR quantum in this project? | 3 |
| 61 | MLFQ queues and slices here? | 3 queues: 2, 4, 8 |
| 62 | MLFQ boost interval here? | every 15 time units |
| 63 | MLFQ demotes a process when? | it uses a whole slice |
| 64 | MLFQ rule that prevents starvation? | R5, the priority boost |
| 65 | Priority convention here? | smaller number = more important |
| 66 | Priority with no priority column behaves as? | FCFS |
| 67 | Does scheduling change total time on one CPU? | No, only who waits |
| 68 | Linux default scheduler today? | EEVDF (CFS before 6.6) |
| 69 | Linux real-time policies? | `SCHED_FIFO`, `SCHED_RR` |
| 70 | Number of automated tests here? | 87 |

---

## Part X — Honest limitations

Interviewers respect "I did not implement that, and here is exactly why" far more
than a bluff. Learn these.

**X1. "Your shell has no `$VAR` expansion."**

Correct. It needs quoting information to survive tokenizing, because `$HOME`
expands in double quotes but not single ones, and my tokenizer currently throws
that away after removing the quotes. I have the `status` built-in as a stand-in
for `$?`. The design change is described in the README's version 2 list.

**X2. "No globbing?"**

No. It is `glob(3)` plus the same quoting problem as above. I chose to get pipes,
redirection and process handling exactly right first, since those are the parts
that teach process management.

**X3. "Ctrl-Z stops your whole shell."**

Yes, and I can show you the process states proving it. It is because I never call
`setpgid()` and never handle SIGTSTP. Doing it properly means process groups,
`tcsetpgrp()`, ignoring SIGTTOU, and `waitpid(..., WUNTRACED)` — it is the top
item on my list rather than something I overlooked.

**X4. "Built-ins do not work in a pipeline."**

Right. `echo hi | cd /tmp` is rejected rather than silently doing nothing. Bash
runs built-ins in the subshell; I decided a clear error beats a half-implemented
feature, and the limitation is in `--help` and the README.

**X5. "Your scheduler has no I/O."**

True, and it is the most significant simplification. Every process is pure CPU,
so nothing ever blocks and MLFQ cannot demonstrate the I/O-bound favouritism it
is designed for. Adding a blocked state and alternating bursts is the first
extension I would make.

**X6. "Only 16 processes and 512 time units?"**

Compile-time limits in `src/scheduler.h`, chosen so the Gantt chart and timeline
stay readable in a terminal. They are enforced with clear errors — including a
check that a workload can finish within the limit, which I added after seeing a
truncated run report a *negative* average waiting time.

**X7. "Your MLFQ configuration is hard-coded."**

Yes, three queues with slices 2/4/8 and a boost every 15 units. The algorithm
already indexes the quantum by queue level, so moving it to command-line options
is mechanical; I left it out to keep version 1 small.

**X8. "Did you copy this from a tutorial?"**

No, and the development log is the evidence: it records the actual compile
errors, the buffering bug that reordered my output, the two `strtok` failures
that made me rewrite the tokenizer, the deliberate experiment where I removed
`close(fd[1])` and captured the hang, and two of my own tests that were wrong
rather than the code. Ask me about any line.

---

## Part Y — Live coding tasks

These are what interviewers ask you to write on a whiteboard from this project.
Practise each one until it takes under five minutes.

**Y1. Write a program that forks a child, has the child run `ls -l`, and has the
parent print the child's exit code.**
Hints: `fork`, `execvp("ls", (char*[]){"ls","-l",NULL})`, `_exit(127)` if exec
fails, `waitpid`, `WIFEXITED`/`WEXITSTATUS`.

**Y2. Implement `cmd1 | cmd2` with two children.**
Hints: `pipe`, `dup2(fd[1], 1)` in the first child, `dup2(fd[0], 0)` in the
second, close every end you do not use *in both processes*, two `wait` calls.

**Y3. Redirect a command's output to a file.**
Hints: `open(path, O_WRONLY|O_CREAT|O_TRUNC, 0644)`, `dup2(fd, 1)`, `close(fd)`,
all in the child before `exec`.

**Y4. Write a function that splits a line into `argv` on spaces, handling double
quotes.**
Hints: character loop, not `strtok`; on `"` copy until the closing quote; bound
every write; NULL-terminate the array.

**Y5. Implement FCFS given arrival and burst arrays; print average waiting time.**
Hints: sort by arrival; `time = max(time, arrival[i])`; `finish = time + burst`;
`wait = finish - arrival - burst`.

**Y6. Implement SJF (non-preemptive).**
Hints: loop until all done; among `arrival <= time && remaining > 0` pick the
minimum burst; if none, `time++`; break ties by arrival.

**Y7. Implement SRTF.**
Hints: same as Y6 but decide every single time unit, compare `remaining`, and
prefer the incumbent on a tie so you do not thrash.

**Y8. Implement Round Robin with a given quantum.**
Hints: a FIFO queue; admit arrivals for the current time *before* requeueing a
preempted process; run `min(quantum, remaining)`; set `start_time` only on the
first dispatch.

**Y9. Implement a circular queue with push, pop and empty.**
Hints: `items[N]`, `head`, `count`; push at `(head + count) % N`; pop at `head`
then `head = (head + 1) % N`; empty is `count == 0`.

**Y10. Reap all finished background children without blocking.**
Hints: `while ((pid = waitpid(-1, &st, WNOHANG)) > 0) { ... }`; handle 0 (none
ready) and −1 (`ECHILD`, none left).

---

## Part Z — HR and behavioural

**Z1. Why did you choose this project?**

Operating systems are the layer I understood least by reading about them. A shell
forces you to actually use `fork`, `exec`, `wait`, file descriptors and signals,
and a scheduler forces you to reason about the policy the kernel applies to the
processes you just created. I wanted the two halves of the same story.

**Z2. What went wrong, and what did you do?**

Several things, and I kept a log of all of them. The most useful was discovering
that `strtok` silently accepted `| wc -l` as a valid command — the shell actually
ran `wc`, which then ate the rest of my test script. It made me rewrite the
tokenizer as a character loop, which also fixed quoting and gave me operators
without spaces for free. Testing invalid input is what found it.

**Z3. How did you decide what to leave out?**

By asking which features teach something I do not already know. Pipes,
redirection and process management were in. String features like `$VAR` and
globbing were out, because they are parsing work rather than OS work, and I wrote
down exactly how I would add them instead of pretending they were unnecessary.

**Z4. Did you work alone?**

Yes, this was an individual course project under Prof. Dalu Jacob. The parts I
would credit are the standard references — the five MLFQ rules follow the usual
textbook formulation, and the exit-code conventions follow POSIX so the shell
behaves like bash.

**Z5. How do you know your code is good quality?**

It compiles with zero warnings under `-Wall -Wextra -std=c11`, it is clean under
AddressSanitizer and UndefinedBehaviorSanitizer including the error paths, it has
87 automated tests whose expected values I derived by hand, and every non-obvious
line has a comment explaining why rather than what.

**Z6. What would you do differently starting over?**

Write the tokenizer as a character loop from the start instead of reaching for
`strtok`, and keep shell state in a struct instead of a global `last_status`. Both
were shortcuts that I ended up paying for.

**Z7. How does this project relate to the job you are applying for?**

It is evidence that I can work close to the system: manage processes and
descriptors correctly, reason about concurrency and deadlock, debug something
that hangs rather than crashes, and choose between algorithms based on measured
trade-offs instead of preference. Those transfer to backend and infrastructure
work directly.

**Z8. What is one thing in this project you are not satisfied with?**

That the scheduler has no I/O bursts. It means MLFQ is being judged on burst
length alone, when the behaviour it is really designed to detect is a process
that blocks quickly. The simulator is correct for what it models; I just want it
to model more.

---

## Final checklist before an interview

- [ ] Can you draw the file-descriptor plumbing for `a | b | c` from memory?
- [ ] Can you explain why `close(fd[1])` in the parent is mandatory, and what
      happens without it?
- [ ] Can you say why `cd` must be a built-in in one sentence?
- [ ] Can you compute FCFS, SJF, SRTF and RR by hand for four processes?
- [ ] Can you state the five MLFQ rules and which one prevents starvation?
- [ ] Can you explain why total time is the same for all six algorithms?
- [ ] Can you name three limitations of your own project before being asked?
- [ ] Can you run `make test` and explain what any failing check means?
