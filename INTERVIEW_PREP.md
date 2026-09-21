# Interview Prep — a senior interviewer's review of this codebase

I read this repository the way I would before interviewing its author: every
source file, the tests, the development log, and then an hour of trying to break
the binaries. What follows is (1) what the project actually is, (2) the questions
I would ask, in the order I would ask them, with the answers I expect and the
file or function the answer should cite, and (3) the weak spots I found, with a
recommendation for how to handle each one when I raise it.

Two ground rules for the candidate. First, cite the code: "in `run_pipeline()`
the parent closes `fd[1]` after every fork" beats "I close the pipe". Second,
when I point at a shortcut, the right move is to name the trade-off you made and
what fixing it would take — never to argue it is not a shortcut. Everything in
§5 was reproduced on the actual binaries, so I will know.

`QUESTIONS.md` in this repository covers the general OS theory (what `fork`
returns, what a zombie is). This document is about *this code*; I assume the
theory and will not repeat it.

---

## 1. What the project actually does

Two C11 programs, no dependencies, ~2,200 lines of source, 87 end-to-end tests.

**`bin/mysh`** (`src/shell.c`, 689 lines, one file) is a Unix shell:

- reads a line with `getline()`, tokenizes it by hand (`tokenize()`) with
  quote and backslash handling, and parses into a pipeline of `struct command`
  (`parse_line()`, `parse_command()`);
- runs built-ins (`cd`, `pwd`, `status`, `help`, `exit`) in-process
  (`run_builtin()`), everything else via `fork()` + `execvp()`
  (`run_pipeline()`, `exec_or_die()`);
- supports `|` up to 16 stages, `<`, `>`, `>>` (`apply_redirection()`), a
  trailing `&` for background jobs, `#` comments;
- ignores SIGINT/SIGQUIT in the shell and restores them in children, reaps
  background jobs with `waitpid(-1, WNOHANG)` before each prompt
  (`reap_background()`), and returns bash-compatible exit codes
  (0/1/2/126/127/128+n via `status_to_code()`).

**`bin/sched`** (`src/sched.c` + `src/scheduler_common.c` + six `src/algo_*.c`)
is a single-CPU, integer-time, tick-driven scheduling simulator:

- workload = `name arrival burst [priority]` lines (`load_workload()`), max 16
  processes, max 512 time units (`workload_fits()` rejects anything bigger);
- six algorithms behind one contract — fill `start_time`/`finish_time`, call
  `add_tick()` per time unit, return the slice count: `run_fcfs`, `run_sjf`,
  `run_srtf`, `run_rr`, `run_priority`, `run_mlfq`;
- prints a Gantt chart (`print_gantt()`), a per-process timeline
  (`print_timeline()`), a metrics table (`print_summary()` over
  `compute_metrics()`), optional CSV (`print_csv()`);
- `compare` runs all six on a private copy of the workload
  (`run_comparison()`) and names the winner per metric.

Reference numbers on `tests/workload1.txt` (P1 0/8, P2 1/4, P3 2/9, P4 3/2),
which several questions below assume:

| algorithm | avg turnaround | avg waiting | avg response | switches |
| --- | --- | --- | --- | --- |
| FCFS | 14.50 | 8.75 | 8.75 | 3 |
| SJF | 12.25 | 6.50 | 6.50 | 3 |
| SRTF | 10.75 | 5.00 | 3.50 | 4 |
| RR (q=3) | 15.75 | 10.00 | 3.00 | 8 |
| PRIORITY | 14.50 | 8.75 | 8.75 | 3 |
| MLFQ | 14.50 | 8.75 | 1.50 | 8 |

---

## 2. The design decisions that matter

These are the choices I will probe. A candidate who can state *why* for each of
them, unprompted, is already ahead.

| decision | where | why it was made | what it cost |
| --- | --- | --- | --- |
| Hand-written character tokenizer instead of `strtok` | `tokenize()` in `shell.c` | `strtok` cannot see quotes and skips empty tokens, which let `\| wc -l` run silently | ~80 lines of careful bounds checking |
| Operators carry an `is_op` flag rather than being recognised by text | `struct token` | distinguishes the operator `>` from the quoted word `">"` | none worth mentioning |
| Built-ins run in the parent, and only as a single command | `run_builtin()`, `execute_line()` | `cd`/`exit` must change the shell's own state; running them in a pipeline child would be meaningless | `echo x \| cd /tmp` is rejected rather than emulated |
| One pipe alive at a time via `prev_read` | `run_pipeline()` | bounded descriptor use, simple close discipline | the fd bookkeeping is the single most error-prone piece of the shell |
| Ignore SIGINT in the shell, reset to default in the child | `main()`, `run_pipeline()` | Ctrl-C kills the command, not the shell, without process groups | background jobs share the group and also die on Ctrl-C (§5.1) |
| Reap with `WNOHANG` at prompt time, no SIGCHLD handler | `reap_background()` | no async-signal-safety problems | zombies accumulate when non-interactive (§5.2) |
| Line-buffered stdout plus `fflush` before `fork` | `main()`, `run_pipeline()` | shell output was appearing after child output when piped | none |
| One `sched` binary, one file per algorithm, if/else dispatch | `sched.c: run_algorithm()`, `algorithms[]` | six `main()`s were duplication; `compare` needs them linkable | adding an algorithm touches three places |
| Tick-based simulation, one time unit per loop iteration | every `run_*()` | mid-slice arrivals and preemption fall out naturally; code is uniform | O(total time) per run instead of O(events); a 1M-unit workload is off the table |
| Slices recorded via `add_tick()` merging | `scheduler_common.c` | one source of truth for Gantt, timeline and switch count | `print_timeline()` is O(n·T·segments) |
| `compare` copies the workload per algorithm | `run_comparison()` | algorithms destroy their input (`remaining`, sorting) | `memcpy` of 16 structs — negligible |
| Up-front `workload_fits()` check | `scheduler_common.c` | a truncated run printed −201 average waiting time and 156% utilisation | a conservative bound (latest arrival + total burst) |
| Fixed arrays and compile-time limits everywhere | `MAX_*` in both headers | no allocation failure paths; every bound is checked | limits are not runtime-configurable |

---

## 3. Core CS concepts this code exercises

- **Process lifecycle**: `fork` (copy-on-write), `execvp` (image replacement,
  PATH search), `waitpid` and status decoding, zombies, orphan reparenting.
- **File descriptors**: the three-level fd → open-file-description → inode
  model, `dup2` as the mechanism of redirection, `O_TRUNC` vs `O_APPEND`,
  descriptor inheritance across `fork`/`exec`.
- **Pipes**: kernel buffer with back-pressure, EOF semantics (all write ends
  closed), SIGPIPE, the deadlock when a parent forgets to close.
- **Signals and terminals**: dispositions across `fork`/`exec` (`SIG_IGN`
  survives `exec`, handlers do not), foreground process groups, why Ctrl-C works
  here and Ctrl-Z does not.
- **stdio buffering**: line vs full buffering, buffer duplication across `fork`,
  `exit` vs `_exit`.
- **Lexing/parsing**: a two-token-class lexer (`word`/`operator`), quoting,
  grammar as `pipeline := command ('|' command)*`, error recovery.
- **Scheduling theory**: preemptive vs non-preemptive, the metrics and their
  identities (`waiting = turnaround − burst`; FCFS has `waiting == response`),
  SJF/SRTF optimality via the exchange argument, the convoy effect, starvation
  and aging, the quantum trade-off, MLFQ's five rules, work conservation (total
  time is invariant across algorithms).
- **Simulation design**: tick-driven vs event-driven, deterministic tie-breaking,
  invariants as tests, the danger of "plausible but wrong" output.
- **Data structures**: circular-buffer FIFO, stable selection sort, run-length
  merged interval list.
- **Engineering hygiene**: bounds checking, `-Wall -Wextra` clean, sanitizers,
  property-based assertions (RR with large quantum ≡ FCFS; priority with no
  priorities ≡ FCFS), an honest development log.

---

## 4. The question bank

Each item: the question, the answer I expect (with citations), then follow-ups
one to three levels deep. **Red flag** notes what a weak answer sounds like.

### Tier 1 — Comprehension: "What does this do?"

**1.1 Walk me through `execute_line()` for `sort < in.txt | uniq -c > out.txt`.**

Expected: `tokenize()` yields `[sort] [<] [in.txt] [|] [uniq] [-c] [>] [out.txt]`
with the three operators flagged `is_op`. `parse_line()` splits on the `|` token
into two ranges and calls `parse_command()` on each: command 0 gets
`words={sort}` and `infile="in.txt"`; command 1 gets `words={uniq,-c}`,
`outfile="out.txt"`, `append=0`. Two commands, so `run_builtin()` is skipped and
`run_pipeline(cmds, 2, 0)` runs.

- Follow-up: *Where do the strings in `cmd->words[]` live?* They point into
  `toks[].text` — nothing is copied. Both arrays are locals of `execute_line()`
  and die together, which is why that is safe.
- Follow-up 2: *What would break if you stored a `struct command` for later?*
  Dangling pointers into a dead stack frame; you would need `strdup`.

**1.2 What does `run_pipeline()` do in the parent after each `fork()`?**

Expected: records `pids[i]`, closes `prev_read` (the child has its own copy),
and if this was not the last stage closes `fd[1]` and keeps `fd[0]` as the new
`prev_read`. After the loop it waits for every started child and takes the
*last* child's status via `status_to_code()`.

- Follow-up: *Why `started` and not `ncmds` in the wait loop?* A failed `pipe()`
  or `fork()` breaks out early; waiting on a pid that was never created blocks
  forever.
- Follow-up 2: *Why is the exit status the last stage's?* POSIX convention;
  `false | true` succeeds. **Red flag**: claiming the shell reports the first
  failure — it does not, and neither does bash without `pipefail`.

**1.3 What does `exec_or_die()` do, and what are the three exit codes?**

Expected: calls `execvp(cmd->words[0], cmd->words)`; anything after that runs
only on failure. `ENOENT` → "command not found", `_exit(127)`. `EACCES` on
something `stat()` says is a directory → "is a directory", `_exit(126)`.
Everything else → `strerror(errno)`, `_exit(126)`.

- Follow-up: *Why `_exit` and not `exit`?* `exit` flushes stdio buffers the child
  inherited from the shell, printing the parent's pending output twice.

**1.4 Read `reap_background()` aloud and explain each token of the `waitpid` call.**

Expected: `waitpid(-1, &wstatus, WNOHANG)`: `-1` any child, `WNOHANG` return 0
immediately if nothing has exited. Loop while it returns a positive pid; 0 and
−1 (`ECHILD`) both end the loop. Called from `main()` only when
`interactive`, just before `print_prompt()`.

**1.5 In `tokenize()`, what makes `ls|wc -l` work without spaces?**

Expected: the word loop's condition `!is_op_char(line[i])` ends a word at any of
`| < > &`, and the operator branch then consumes it. `>>` is detected by a
one-character lookahead before the generic operator case.

- Follow-up: *What does `echo ">"` produce?* A word token with text `>` and
  `is_op == 0`, so `parse_command()` treats it as an argument.

**1.6 Describe the contract every `run_*()` function satisfies.**

Expected (from `scheduler.h`): take `procs[]`, set `start_time` on first
dispatch and `finish_time` on completion, call `add_tick(segs, &nsegs, proc, t)`
for every time unit including idle (`proc == -1`), return `nsegs`. Nothing else;
metrics are computed afterwards by `compute_metrics()`.

**1.7 What does `add_tick()` do when the same process runs two ticks in a row?**

Expected: extends `segs[nsegs-1].end` instead of appending, so 8 consecutive
ticks become one segment. That merge is what the Gantt chart and the
context-switch count rely on.

- Follow-up: *Can `segs[]` overflow?* No, by construction: each tick adds at most
  one segment, ticks ≤ `MAX_TIME` = 512, and `MAX_SEGMENTS` = 1024. The
  `warning: too many CPU slices` branch is unreachable with current constants.
  **Red flag**: not knowing whether the guard can fire.

**1.8 State the five MLFQ rules and point to the code for each.**

Expected, all in `run_mlfq()` in `src/algo_mlfq.c`: R1 admit arrivals to
`queues[0]`; R5 boost every `MLFQ_BOOST_INTERVAL` (15) — drain `queues[1..]` into
`queues[0]` *and* separately reset the running process; R4 preempt if any queue
strictly above `procs[running].queue` is non-empty, pushing the victim to the
tail of its own queue; R2 pick from the highest non-empty queue; R3 after the
tick, if `used == mlfq_quantum[queue]` demote (or requeue at the bottom).
Completion is checked before demotion.

**1.9 In `run_rr()`, why are arrivals admitted before the quantum-expiry check?**

Expected: so a process arriving exactly when a slice expires is queued *ahead*
of the preempted process — the conventional behaviour. Swapping the two blocks
changes the Gantt chart. The candidate should know this is a convention choice,
not a law.

**1.10 What does `--csv` print, and how is the header suppressed in `compare`?**

Expected: `print_csv(label, procs, n, with_header)` prints one row per process
(`algorithm,process,arrival,burst,priority,start,finish,turnaround,waiting,response`);
`run_comparison()` passes `a == 0` so only the first algorithm emits the header.

### Tier 2 — Design: "Why did you build it this way?"

**2.1 Why can `cd` not be forked like any other command?**

Expected: `fork()` copies the shell's state; `chdir()` in the child changes the
child, which then exits. So `builtin_cd()` runs in-process. Same reasoning for
`exit`, `pwd`, `status`.

- Follow-up: *Then why refuse built-ins inside a pipeline instead of running them
  in the child like bash?* Because in a child `cd` and `exit` would silently do
  nothing useful; a syntax error is more honest than a no-op. Trade-off: `echo
  hi | cd /tmp` is legal bash and illegal here.
- Follow-up 2: *What about `exit &`?* Currently it is not treated as a built-in
  (`!background` gate in `execute_line()`) and falls through to
  `execvp("exit")` → "command not found". Defensible as an edge nobody types;
  should still be acknowledged as a quirk (§5.7).

**2.2 Why does the shell ignore SIGINT rather than install a handler?**

Expected: with no process groups, the terminal delivers SIGINT to the shell *and*
its children. `SIG_IGN` in the shell plus `SIG_DFL` in the child (set in
`run_pipeline()` after `fork`) gives the desired behaviour with zero
async-signal-safety concerns. The subtle part: `SIG_IGN` survives `exec`, so the
child must reset it or `sleep` becomes unkillable.

- Follow-up: *What does this design get wrong?* Background jobs are in the same
  process group and also receive the SIGINT — see §5.1. A strong candidate
  raises this before I do.

**2.3 Why one pipe at a time with `prev_read`, instead of creating all pipes up front?**

Expected: bounded open descriptors (three at most in the parent at any moment),
and a simpler close discipline — each iteration closes exactly what it no longer
needs. Creating `n−1` pipes first works too but requires closing `2(n−1)` fds in
every child, which is where most student shells leak.

- Follow-up: *Why does the child close `fd[0]` even though it only writes?* It
  inherited it. A stray read end held open by a sibling can prevent EOF
  downstream in more complex topologies and is a descriptor leak regardless.

**2.4 Why did the tokenizer move from `strtok` to a hand-written loop?**

Expected: two observed failures, both in the development log (README §11.4–11.5):
quotes were invisible (`tr ' ' '\n'` became four words), and empty tokens were
skipped so `| wc -l` ran `wc` — which then consumed the rest of the script from
stdin. The rewrite added operator-without-spaces for free.

- Follow-up: *What information does the tokenizer throw away that you will need
  later?* Whether each character was quoted. `$HOME` must expand in double quotes
  and not in single quotes; `*.c` must glob only when unquoted. Adding either
  feature means the token needs per-character quoting state. This is the honest
  answer to "why no variables" (§5.9).

**2.5 Why a single `sched` binary with an if/else dispatch rather than function pointers?**

Expected: six `main()`s were duplication and made `compare` impossible.
`run_algorithm()` in `sched.c` is an if/else chain because the six functions
have different signatures — `run_rr` needs `quantum`, `run_mlfq` needs
`verbose` — and a pointer table would force a lowest-common-denominator signature
that hides real differences.

- Follow-up: *Cost of that choice?* Adding an algorithm touches `scheduler.h`,
  the `algorithms[]` table, `run_algorithm()`, and the Makefile's `SCHED_SRC`.
  Four places is acceptable at six algorithms; at sixty I would reconsider.

**2.6 Why tick-based simulation rather than event-driven?**

Expected: one tick per iteration makes preemption trivial — a mid-slice arrival is
seen at the next tick — and gives all six algorithms the same loop shape, which
is the pedagogical goal. Cost: O(T) time where T is total time, versus O(E log E)
for an event queue. Irrelevant at T ≤ 512; decisive at T = 10⁷.

**2.7 Why does `compare` `memcpy` the workload per algorithm?**

Expected: algorithms mutate `remaining`, `start_time`, `finish_time`, `queue`,
and `run_fcfs()` sorts the array in place. Sharing would make every algorithm
after the first "finish" instantly and print plausible garbage. The test "compare
gives every algorithm the same total time" guards the invariant.

- Follow-up: *Why is that invariant true?* All six are work-conserving; the CPU
  never idles while a process is ready, so total time = latest point at which
  work runs out, independent of order.

**2.8 Why are the tie-breaks the way they are?**

Expected: `pick_shortest()`/`pick_most_important()` break ties by earlier
arrival, then array index — deterministic and FCFS-like. `pick_shortest_remaining()`
seeds `best = current` and compares with strict `<`, so the incumbent wins ties
and the CPU does not ping-pong between equal processes. MLFQ preempted processes
go to the *tail* of their queue. Each is a documented convention; the candidate
should know that textbooks differ and that changing one changes the output.

**2.9 Why `workload_fits()` up front rather than a check at the end?**

Expected: the failure it prevents was observed — average waiting −201, utilisation
156% — and looked like a result rather than an error. Rejecting with a message
before simulating is cheaper and clearer than detecting nonsense afterwards. The
bound `latest_arrival + total_burst` is a true upper bound for any
work-conserving scheduler.

**2.10 Why are metrics computed in `compute_metrics()` rather than inside each algorithm?**

Expected: single source of truth; an algorithm that computed its own averages
could be wrong in a way the others are not. Algorithms report facts
(`start_time`, `finish_time`, slices); derivation happens once.

### Tier 3 — Edge cases and failure modes: "What happens if…?"

I ran each of these. The candidate should either know the answer or reason to it
quickly; guessing confidently in the wrong direction is the worst outcome.

**3.1 `sleep 30 &`, then press Ctrl-C at the prompt.**

Actual: the background `sleep` dies (`[done] pid N exited with status 130`).
Reason: no `setpgid()`, so the job shares the shell's process group, which is the
terminal's foreground group; the child reset SIGINT to default. Bash protects
background jobs. See §5.1 for how to defend this.

**3.2 Press Ctrl-Z during a foreground command.**

Actual: both `mysh` and the command enter state `T`; the parent bash prints
`[1]+ Stopped ./bin/mysh`. Reason: SIGTSTP is neither ignored nor handled, and
there are no process groups. Follow-up: *why did an early automated test show
Ctrl-Z doing nothing?* The harness left the shell in an orphaned process group,
and POSIX discards SIGTSTP to orphaned groups. A candidate who knows this detail
has genuinely explored the code.

**3.3 Run a script non-interactively with `sleep 0.2 &` followed by a long command.**

Actual: `ps` shows the finished `sleep` as `Z` (zombie) until the shell exits.
Reason: `reap_background()` is only called inside `if (interactive)` in
`main()`. Harmless at this scale; a long-running script spawning thousands of
background jobs would exhaust pids. Fix is one line (call it unconditionally) or
a SIGCHLD handler.

**3.4 Run `./bin/mysh > out.txt` from a terminal.**

Actual: `out.txt` contains `mysh:/tmp$ hello\nmysh:/tmp$ ` — the prompts. Reason:
`print_prompt()` writes to stdout; bash writes prompts to stderr precisely so
redirection does not capture them. Trivial fix, real oversight.

**3.5 Type `echo hi\` and press Enter.**

Actual: prints `hi` followed by an *extra* blank line — the argument is `hi\n`.
Reason: `getline` keeps the `\n`, and the escape rule `line[i]=='\\' &&
line[i+1] != '\0'` treats the newline as an escaped literal character. A real
shell would prompt for a continuation line. Low severity, but it is a correctness
bug in `tokenize()`.

**3.6 `cd ~` or `cd ~/proj`.**

Actual: `mysh: cd: ~: No such file or directory`. Reason: no tilde expansion
anywhere; `builtin_cd()` only special-cases *no* argument (→ `$HOME`). Users hit
this within a minute.

**3.7 A pipeline where `fork()` fails on the third of four stages.**

Expected reasoning from `run_pipeline()`: `perror`, close the just-created pipe,
`break`. `started` is 2, so the wait loop reaps the two live children. Stage 2's
stdout is a pipe whose read end has now been closed by the parent and never
inherited, so stage 2 gets SIGPIPE on write and dies; the user sees a truncated
pipeline and an error. Reasonable degradation, but the status reported is stage
2's, not an error code — worth acknowledging.

**3.8 `apply_redirection()` fails in the child (missing input file).**

Expected: message via `strerror(errno)`, `_exit(1)` — the child must *not*
proceed to `exec` with the wrong stdin. The parent sees status 1. Follow-up:
*why does the message not interleave badly with the shell's output?* stderr is
unbuffered, and stdout is flushed before `fork`.

**3.9 A workload with a comment line longer than 255 characters.**

Actual: `error: file line 2: expected "name arrival burst [priority]"`. Reason:
`load_workload()` uses `fgets(line, 256, fp)`; a longer line is delivered in two
chunks, the second of which lacks the leading `#` and is parsed as data. The
reported line number is also wrong, because `lineno` counts chunks. Fix: detect
the missing `\n` and discard to end of line, or use `getline`.

**3.10 `./bin/sched fcfs -q 5 file`.**

Actual: accepted silently; `-q` is parsed unconditionally in `main()` and only
`run_rr()` reads it. Harmless but a UX smell — an interviewer will ask why an
option that does nothing is not rejected.

**3.11 Two processes named `X` in the workload.**

Actual: accepted; the Gantt chart shows `| X | X |`. Metrics are correct because
everything is by index, but the output is ambiguous. `load_workload()` should
reject duplicates.

**3.12 What if two processes have equal remaining time in SRTF and the tie-break were `<=`?**

Expected: the CPU would alternate between them every tick; averages unchanged,
context switches roughly doubled, Gantt chart unreadable. This is exactly why
`pick_shortest_remaining()` seeds with the incumbent and uses `<`. **Red flag**:
saying tie-breaks "don't matter".

**3.13 What if `MAX_TIME` were raised to 100,000?**

Expected: `print_timeline()` becomes the bottleneck — it calls `who_ran_at()`
(a linear scan of segments) once per process per tick, O(n·T·S). SRTF's per-tick
O(n) scan also grows. The right answer names both and proposes a per-tick
`owner[]` array (O(T) memory) and a heap for SRTF.

**3.14 A 20,000-character command line.**

Expected: `getline` grows the buffer fine; `tokenize()` reports either "line has
too many words (limit is 128)" or "word longer than 255 characters" and returns
−1; `last_status` becomes 2. No crash — every write into `toks[n].text` is
guarded by `len == MAX_TOKEN - 1`. Sanitizers confirm.

**3.15 `echo ""` — is the empty argument preserved?**

Actual: yes; `/usr/bin/printf "[%s]" ""` prints `[]`. Reason: in `tokenize()`
`n++` runs after the word loop even when `len == 0`, so an empty quoted word is a
real token. Good: many student shells drop it.

### Tier 4 — Extension: "How would you scale or extend this?"

**4.1 Add real job control (`jobs`, `fg`, `bg`, working Ctrl-Z).**

Expected plan: `setpgid(0, 0)` in the child (and `setpgid(pid, pid)` in the
parent to avoid the race) so each pipeline is its own process group, with later
stages joining the first's group; `tcsetpgrp(STDIN_FILENO, pgid)` to hand the
terminal to a foreground job and back to the shell; the shell ignores SIGTTOU
while doing so; `waitpid(-pgid, &st, WUNTRACED)` to observe stops; a job table
keyed by pgid; `fg` = `tcsetpgrp` + `kill(-pgid, SIGCONT)` + wait; `bg` =
`SIGCONT` without wait. This also fixes §5.1, because background jobs are no
longer in the foreground group.

- Follow-up: *What new bug does this introduce if you forget SIGTTOU?* The shell
  stops itself when it calls `tcsetpgrp` from the background.

**4.2 Add `$VAR`, `$?`, and globbing.**

Expected: extend `struct token` (or a parallel array) with per-character quoting
state; add an expansion pass between `tokenize()` and `parse_line()` that
substitutes `$NAME` (via `getenv`) and `$?` (from `last_status`) in unquoted and
double-quoted regions, then `glob(3)` on unquoted words containing `*?[`. Word
splitting of expansion results is the notorious part — say so.

**4.3 Add `&&`, `||`, `;`.**

Expected: a layer above `parse_line()`: split tokens on those operators into a
list of pipelines with connectors, run sequentially consulting `last_status`.
Requires the status to stop being a bare global if you want it testable — a good
moment to introduce a `struct shell`.

**4.4 Make the scheduler event-driven.**

Expected: a min-heap of events (arrival, slice expiry, completion, boost) keyed by
time; the loop pops the next event instead of stepping ticks. Complexity drops
from O(T) to O(E log E). Cost: preemption logic becomes explicit (a new arrival
must compute whether it displaces the running process and reschedule its
completion/expiry event), and the six algorithms lose their uniform shape.

**4.5 Add I/O bursts.**

Expected: `struct process` gets a list of alternating CPU/I/O bursts and a
`blocked_until` field; a blocked state; on CPU-burst completion the process moves
to blocked, then back to ready. MLFQ should *not* demote a process that blocks
before exhausting its slice — that is how it detects interactivity. Metrics gain
blocked time; utilisation becomes interesting because the CPU can idle with work
outstanding. This is the extension that makes MLFQ's design point demonstrable.

**4.6 Multi-core.**

Expected: `running` becomes `running[ncpu]`; choose between one global ready queue
(simple, contended, cache-hostile) and per-CPU queues with periodic load
balancing or work stealing (Linux). Introduce affinity and migration cost; total
time is no longer invariant across algorithms, so the `compare` invariant test
must change.

**4.7 Make MLFQ configuration runtime.**

Expected: `struct mlfq_config {int nqueues; int quantum[]; int boost;}` passed to
`run_mlfq()`; parse `--queues`, `--quantum a,b,c`, `--boost`. The algorithm
already indexes `mlfq_quantum[queue]`, so the change is mechanical.

**4.8 Harden the test suite.**

Expected: keep the end-to-end bash checks but add (a) invariant tests over random
workloads — every process finishes, busy time equals total burst, no overlapping
slices, no algorithm beats SRTF's average waiting time; (b) a pty-driven test for
Ctrl-C/Ctrl-Z/background reaping; (c) unit tests for `tokenize()` and
`parse_line()` by linking `shell.c` with a test `main` (requires making the
functions non-`static` or including the .c file). The README §11.12 incident
(an `awk` pattern that matched a prose line) is the argument for matching
structure, not words.

**4.9 Where would you put a performance budget?**

Expected: nowhere yet — measure first. `perf stat ./bin/sched compare` will show
microseconds. Optimisation questions about this code are about *complexity
class* for larger inputs (§3.13), not about the current binary.

---

## 5. Weak links and shortcuts — and how to handle them

Everything here was reproduced on the current binaries. Severity is my judgment
of how much it would cost the candidate if they were surprised by it.

| # | issue | where | severity | reproduce |
| - | ----- | ----- | -------- | --------- |
| 5.1 | Background jobs killed by Ctrl-C at the prompt | `run_pipeline()` signal reset, no `setpgid` | **high** | `sleep 30 &` then Ctrl-C → `exited with status 130` |
| 5.2 | Zombies accumulate in non-interactive mode | `main()` calls `reap_background()` only if interactive | medium | `printf 'sleep 0.2 &\nsleep 1\n' \| mysh` and `ps` → `Z` |
| 5.3 | Ctrl-Z stops the whole shell | no SIGTSTP handling, no process groups | medium (documented) | `sleep 20`, Ctrl-Z → `[1]+ Stopped ./bin/mysh` |
| 5.4 | Prompt written to stdout | `print_prompt()` | low | `mysh > f` from a tty; `f` contains prompts |
| 5.5 | Trailing `\` swallows the newline into the argument | `tokenize()` escape rule | low | `echo hi\` prints an extra blank line |
| 5.6 | No tilde expansion | `builtin_cd()` / no expansion pass | low–medium (very visible) | `cd ~` fails |
| 5.7 | `exit &` → "command not found" | `execute_line()` `!background` gate | low | `exit &` |
| 5.8 | `last_status` is a file-scope global | `shell.c` | low (design) | — |
| 5.9 | Tokenizer discards quoting state, blocking `$VAR`/globbing | `struct token` | medium (design debt) | — |
| 5.10 | `> file` alone is rejected (bash creates the file) | `parse_command()` "missing command" | low (behavioural difference) | `> out.txt` |
| 5.11 | Workload lines > 255 chars are split; long comments break parsing with a wrong line number | `load_workload()` `fgets(line, 256)` | medium | 300-char `#` line → `line 2: expected …` |
| 5.12 | `-q` silently accepted for non-RR algorithms | `sched.c: main()` | low | `sched fcfs -q 5` |
| 5.13 | Duplicate process names accepted | `load_workload()` | low | `X 0 2` / `X 1 2` |
| 5.14 | Circular queue implemented twice | `algo_rr.c` and `algo_mlfq.c` both define `q_init/q_push/q_pop/q_empty` | low (duplication) | grep |
| 5.15 | `print_timeline()` is O(n·T·S) | `who_ran_at()` linear scan per cell | low at current limits | raise `MAX_TIME` |
| 5.16 | SJF/SRTF/Priority do O(n) scans per decision | `pick_*()` | low at n ≤ 16 | — |
| 5.17 | Tests are end-to-end string matches only; interactive behaviour untested | `tests/run_tests.sh` | medium | the `awk` incident in README §11.12 |
| 5.18 | No I/O, single CPU, zero-cost context switches | model | medium (scope) | — |
| 5.19 | `signal()` rather than `sigaction()` | `main()`, `run_pipeline()` | low (portability) | — |

### How to handle each when I raise it

**5.1 Background jobs die on Ctrl-C.** Do not defend this; it is a real bug and
I will have reproduced it. The right answer: "Yes — the job is in the shell's
process group because I never call `setpgid()`, and the child restores SIGINT to
default, so the terminal's SIGINT reaches it. Bash either puts background jobs in
their own group or leaves SIGINT ignored for them when job control is off. The
one-line mitigation is to skip the `signal(SIGINT, SIG_DFL)` reset when
`background` is set; the proper fix is process groups, which also fixes Ctrl-Z."
Naming both the cheap and the correct fix is what I want to hear.

**5.2 Zombies when non-interactive.** Acknowledge, explain the reason (reaping
is tied to the prompt), and note the harm is bounded — they are reaped at exit or
by init. Fix: call `reap_background()` every loop iteration, or a SIGCHLD handler
that sets a flag. Bonus: mention that a handler must be async-signal-safe.

**5.3 Ctrl-Z.** This one is documented in the README, so say so, then give the
4.1 plan. I will be checking whether you know *why* the harness first showed
nothing (orphaned process group discards SIGTSTP).

**5.4 Prompt on stdout.** "Bash writes the prompt to stderr so `mysh > file`
does not capture it. One-line fix in `print_prompt()`." Do not overthink it.

**5.5 Trailing backslash.** "The escape rule sees the `\n` that `getline` kept
and treats it as a literal. A real shell would issue a continuation prompt. The
fix is to check for `\\\n` before the general escape and either strip it or read
another line." Shows you understand what `getline` returns.

**5.6 `cd ~`.** "There is no expansion pass at all yet. Tilde belongs with
`$VAR` in the same pass (§4.2). For `cd` alone I could special-case a leading
`~` with `getenv("HOME")` in five lines." Admit it is the most user-visible gap.

**5.7 `exit &`.** Edge case; say the gate exists so a background built-in is not
run in the parent, and that a cleaner rule is "built-ins ignore `&`" or "reject
`&` on a built-in with a message".

**5.8 The global.** "A file-scope `static` is the honest representation of
shell-wide state in a one-file program, but it blocks testing and `$?`. The next
refactor is a `struct shell` passed through `execute_line()`." Do not pretend it
is good design; say it is a deliberate deferral.

**5.9 Quoting state.** This is the most important design debt to be able to
explain. "The tokenizer removes quotes and forgets where they were. Every
expansion feature needs that information, so adding `$VAR` correctly means
changing `struct token` first. I chose to spend version 1 on process management
rather than on parsing." That is a defensible prioritisation — as long as you
know it is the blocker.

**5.10 `> file` alone.** Behavioural difference from bash, not a bug; say which
behaviour you chose and why (a command with no program is more likely a typo than
an intentional truncate). Accept that a bash user would expect the file.

**5.11 Long lines in the loader.** Real bug with a wrong error message. "`fgets`
with a 256-byte buffer splits long lines; the tail of a long comment has no `#`.
Fix: after `fgets`, if the buffer has no `\n`, consume to end of line before
continuing, and count lines rather than chunks — or use `getline`." Do not argue
that 255 characters is enough.

**5.12 `-q` accepted everywhere.** "Options are parsed before the algorithm is
known. Fix: after dispatch, warn or error if `-q` was given for anything but
`rr`." Small; concede immediately.

**5.13 Duplicate names.** Concede; a `strcmp` loop in `load_workload()`.

**5.14 Duplicated queue.** "The two copies are identical; they should be one
`queue.h`/`queue.c` linked into both. I kept each algorithm file self-contained
for readability and paid with 40 duplicated lines." That is a real trade-off, but
know that a reviewer will call it out.

**5.15–5.16 Complexity.** Answer with the complexity class and the fix (per-tick
owner array; heap keyed on remaining time), then say that at n ≤ 16 and T ≤ 512
it is measured in microseconds and optimising now would be premature. Both parts
matter: knowing the fix and knowing not to apply it yet.

**5.17 Test style.** Own it: "End-to-end string checks caught real bugs, but they
are brittle — one of my own assertions matched a prose line. I would add
invariant tests over random workloads and unit tests for the tokenizer." Mention
that every expected value was hand-computed; that is your strongest point here.

**5.18 Model scope.** State the three simplifications plainly (pure CPU bursts,
one CPU, free switches) and what each hides: MLFQ's I/O-bound favouritism,
load balancing, and the true cost of a small quantum. Then say which you would add
first (I/O bursts) and why (it is the one MLFQ exists for).

**5.19 `signal()`.** "Portable code uses `sigaction()` because `signal()`'s
semantics differ across Unixes. On Linux/glibc with `SIG_IGN`/`SIG_DFL` only,
the difference is nil." Correct and concise.

---

## 6. What a strong candidate volunteers before being asked

Calibration for the interviewer, and a target for the candidate:

- Names 5.1 (background jobs and Ctrl-C) as the most serious known bug, with both
  fixes.
- Explains the `fd[1]` close in the parent by describing what happens without it
  (a hang, exit code 124 under `timeout`), not by reciting a rule.
- Knows that FCFS's average waiting and response times are equal by necessity,
  and that total time is the same for all six algorithms and why.
- Can say which tie-break conventions were chosen and that they are conventions.
- Knows that `segs[]` cannot overflow and can prove it from the constants.
- Distinguishes "documented limitation I chose" (Ctrl-Z, built-ins in pipelines,
  no `$VAR`) from "bug I found afterwards" (5.1, 5.5, 5.11) — and is comfortable
  saying both.
- When asked about scale, answers with complexity classes, then declines to
  optimise a program that runs in microseconds.

Red flags, in the order I weight them: not knowing what a line they wrote does;
arguing that a reproduced bug is intended behaviour; claiming a metric improves
"in general" without naming the workload; treating tie-breaks as irrelevant;
being unable to say what they would do next.

---

## 7. A 45-minute interview plan using this document

| minutes | what | pull from |
| ------- | ---- | --------- |
| 0–5 | Candidate's own summary; I listen for design decisions, not features | §1–2 |
| 5–15 | Trace one command end to end; probe the fd closes | 1.1, 1.2, 2.3 |
| 15–22 | One shell edge case they did not expect | 3.1, 3.3 or 3.5 |
| 22–32 | Scheduler: contract, MLFQ rules, one hand computation | 1.6, 1.8, and any row of the §1 table |
| 32–38 | One design-debt item and the honest answer | 5.9 or 5.14 |
| 38–45 | Extension: pick 4.1 or 4.5 depending on which half they were stronger in | Tier 4 |

If the candidate reaches the Tier 4 discussion having conceded the §5 items
gracefully and cited functions throughout, that is a hire signal for a junior
systems role.
