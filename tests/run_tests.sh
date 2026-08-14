#!/bin/bash
#
# run_tests.sh - checks that the shell and all six schedulers still behave.
#
#   ./tests/run_tests.sh          (or: make test)
#
# Every test does the same three things: run something, compare the output or
# the exit code with what we expect, print PASS or FAIL. The script exits with
# status 1 if anything failed, so it can be used in a CI pipeline later.

# Run from the project root no matter where the script was called from.
cd "$(dirname "$0")/.." || exit 1

SHELL_BIN=./bin/mysh
SCHED_BIN=./bin/sched

passed=0
failed=0

# A scratch directory so the tests never touch the real project files.
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

# ---------------------------------------------------------------- helpers ---

# ok <description> <expected> <actual>
ok() {
    if [ "$2" = "$3" ]; then
        printf '  PASS  %s\n' "$1"
        passed=$((passed + 1))
    else
        printf '  FAIL  %s\n' "$1"
        printf '        expected: [%s]\n' "$2"
        printf '        actual:   [%s]\n' "$3"
        failed=$((failed + 1))
    fi
}

# contains <description> <needle> <haystack>
contains() {
    case "$3" in
        *"$2"*)
            printf '  PASS  %s\n' "$1"
            passed=$((passed + 1))
            ;;
        *)
            printf '  FAIL  %s\n' "$1"
            printf '        expected to find: [%s]\n' "$2"
            printf '        in output:        [%s]\n' "$3"
            failed=$((failed + 1))
            ;;
    esac
}

# Feed the shell a script on stdin and capture stdout+stderr together.
run_shell() {
    printf '%s\n' "$1" | $SHELL_BIN 2>&1
}

# ------------------------------------------------------------------ checks ---

for prog in "$SHELL_BIN" "$SCHED_BIN"; do
    if [ ! -x "$prog" ]; then
        echo "error: $prog is missing - run 'make' first"
        exit 1
    fi
done

echo
echo "=== shell: running commands ==="

ok "echo prints its arguments" \
   "hello world" \
   "$(run_shell 'echo hello world')"

ok "quotes keep the spaces inside them" \
   "hello   world" \
   "$(run_shell 'echo "hello   world"')"

ok "single quotes work too" \
   "a b c" \
   "$(run_shell "echo 'a b c'")"

ok "a program is found through PATH" \
   "3" \
   "$(run_shell 'printf "a\nb\nc\n" | wc -l' | tr -d ' ')"

echo
echo "=== shell: pipes ==="

ok "two-stage pipeline" \
   "2" \
   "$(run_shell 'printf "x\ny\n" | wc -l' | tr -d ' ')"

ok "three-stage pipeline" \
   "b" \
   "$(run_shell 'printf "a\nb\nc\n" | grep b | head -1')"

ok "pipes work without spaces around them" \
   "1" \
   "$(run_shell 'echo hi|wc -l' | tr -d ' ')"

echo
echo "=== shell: redirection ==="

run_shell "echo first > $work/out.txt" > /dev/null
ok "output redirection creates the file" \
   "first" \
   "$(cat "$work/out.txt")"

run_shell "echo second >> $work/out.txt" > /dev/null
ok "append redirection keeps the old content" \
   "first
second" \
   "$(cat "$work/out.txt")"

run_shell "echo third > $work/out.txt" > /dev/null
ok "plain > overwrites the file" \
   "third" \
   "$(cat "$work/out.txt")"

printf 'pear\napple\npear\n' > "$work/in.txt"
ok "input redirection plus a pipe" \
   "apple
pear" \
   "$(run_shell "sort < $work/in.txt | uniq")"

echo
echo "=== shell: error handling ==="

contains "unknown command is reported" \
   "command not found" \
   "$(run_shell 'nosuchcommand')"

ok "unknown command sets status 127" \
   "127" \
   "$(run_shell 'nosuchcommand
status' | tail -1)"

ok "a failing program sets its exit status" \
   "1" \
   "$(run_shell 'false
status' | tail -1)"

ok "a successful program sets status 0" \
   "0" \
   "$(run_shell 'true
status' | tail -1)"

contains "running a directory is refused" \
   "is a directory" \
   "$(run_shell '/tmp')"

contains "empty pipeline stage is a syntax error" \
   "syntax error near '|'" \
   "$(run_shell '| wc -l')"

contains "trailing pipe is a syntax error" \
   "syntax error near '|'" \
   "$(run_shell 'ls |')"

contains "missing file name is a syntax error" \
   "expected a file name after '>'" \
   "$(run_shell 'echo x >')"

contains "unterminated quote is a syntax error" \
   "unterminated" \
   "$(run_shell 'echo "oops')"

contains "misplaced & is a syntax error" \
   "syntax error near '&'" \
   "$(run_shell 'sleep 1 & echo hi')"

contains "cd into a missing directory is reported" \
   "No such file or directory" \
   "$(run_shell 'cd /no/such/place')"

contains "reading a missing file is reported" \
   "No such file or directory" \
   "$(run_shell "cat < $work/missing.txt")"

echo
echo "=== shell: built-ins ==="

ok "cd changes the working directory" \
   "/tmp" \
   "$(run_shell 'cd /tmp
pwd')"

ok "exit passes its argument back to the caller" \
   "7" \
   "$(printf 'exit 7\n' | $SHELL_BIN > /dev/null 2>&1; echo $?)"

contains "help lists the built-ins" \
   "built-in commands" \
   "$(run_shell 'help')"

ok "a comment line does nothing" \
   "" \
   "$(run_shell '# just a comment')"

echo
echo "=== shell: background jobs ==="

contains "a background command prints its pid" \
   "[background] pid" \
   "$(run_shell 'sleep 0.1 &')"

echo
echo "=== FCFS ==="

fcfs_out=$($SCHED_BIN fcfs tests/workload1.txt)

contains "FCFS runs P1 first" "| P1 | P2 | P3 | P4 |" "$fcfs_out"
contains "FCFS average waiting time is 8.75" \
         "average waiting time    : 8.75" "$fcfs_out"
contains "FCFS average turnaround time is 14.50" \
         "average turnaround time : 14.50" "$fcfs_out"
contains "FCFS keeps the CPU 100% busy on this workload" \
         "CPU utilisation         : 100.0%" "$fcfs_out"

fcfs_idle=$($SCHED_BIN fcfs tests/workload3.txt)
contains "FCFS shows an idle block when nothing has arrived" "idle" "$fcfs_idle"

echo
echo "=== SJF (shortest job first) ==="

sjf_out=$($SCHED_BIN sjf tests/workload1.txt)

contains "SJF runs the short late arrival P4 before the longer P2 and P3" \
         "| P1 | P4 | P2 | P3 |" "$sjf_out"
contains "SJF average turnaround time is 12.25" \
         "average turnaround time : 12.25" "$sjf_out"
contains "SJF average waiting time (6.50) beats FCFS (8.75)" \
         "average waiting time    : 6.50" "$sjf_out"
contains "SJF needs no extra context switches compared with FCFS" \
         "context switches        : 3" "$sjf_out"

echo
echo "=== SRTF (preemptive shortest job first) ==="

srtf_out=$($SCHED_BIN srtf tests/workload1.txt)

contains "SRTF preempts P1 as soon as the shorter P2 arrives" \
         "| P1 | P2 | P4 | P1 | P3 |" "$srtf_out"
contains "SRTF has the best average waiting time (5.00)" \
         "average waiting time    : 5.00" "$srtf_out"
contains "SRTF average turnaround time is 10.75" \
         "average turnaround time : 10.75" "$srtf_out"
contains "SRTF average response time is 3.50" \
         "average response time   : 3.50" "$srtf_out"

echo
echo "=== Round Robin ==="

rr_out=$($SCHED_BIN rr -q 3 tests/workload1.txt)

contains "RR with quantum 3 gives everyone an early turn" \
         "average response time   : 3.00" "$rr_out"
contains "RR with quantum 3 has a worse waiting time (10.00) than FCFS" \
         "average waiting time    : 10.00" "$rr_out"
contains "RR reports the quantum it used" "Quantum: 3 time units" "$rr_out"

contains "RR with a quantum larger than every burst behaves like FCFS" \
         "average turnaround time : 14.50" \
         "$($SCHED_BIN rr -q 20 tests/workload1.txt)"
contains "RR with quantum 1 switches on every single time unit" \
         "context switches        : 20" \
         "$($SCHED_BIN rr -q 1 tests/workload1.txt)"

echo
echo "=== Priority ==="

prio_out=$($SCHED_BIN priority tests/workload4.txt)

contains "priority 1 (HIGH) is served before priority 2 (MID)" \
         "| LOW | HIGH | MID | BG |" "$prio_out"
contains "priority average turnaround time is 10.00" \
         "average turnaround time : 10.00" "$prio_out"
contains "the least important process (BG) is served last" \
         "BG                  3        5       13       18" "$prio_out"
contains "without a priority column, priority scheduling equals FCFS" \
         "average turnaround time : 14.50" \
         "$($SCHED_BIN priority tests/workload1.txt)"

echo
echo "=== MLFQ ==="

mlfq_out=$($SCHED_BIN mlfq tests/workload1.txt)

contains "MLFQ demotes a process that uses its whole slice" \
         "demoted to Q1" "$mlfq_out"
contains "MLFQ boosts priorities periodically" \
         "priority boost" "$mlfq_out"
contains "MLFQ response time (1.50) beats FCFS (8.75)" \
         "average response time   : 1.50" "$mlfq_out"

mlfq_out2=$($SCHED_BIN mlfq tests/workload2.txt)
contains "MLFQ preempts a low-priority process when work arrives" \
         "preempted" "$mlfq_out2"
contains "MLFQ finishes the short late arrival quickly" \
         "C finished" "$mlfq_out2"

echo
echo "=== compare mode ==="

cmp_out=$($SCHED_BIN compare tests/workload1.txt)

contains "compare lists FCFS"     "FCFS"     "$cmp_out"
contains "compare lists SJF"      "SJF"      "$cmp_out"
contains "compare lists SRTF"     "SRTF"     "$cmp_out"
contains "compare lists RR"       "RR"       "$cmp_out"
contains "compare lists PRIORITY" "PRIORITY" "$cmp_out"
contains "compare lists MLFQ"     "MLFQ"     "$cmp_out"
contains "compare picks SRTF as best for waiting time" \
         "best average waiting    : SRTF (5.00)" "$cmp_out"
contains "compare picks MLFQ as best for response time" \
         "best average response   : MLFQ (1.50)" "$cmp_out"
contains "compare picks FCFS for fewest context switches" \
         "fewest context switches : FCFS (3)" "$cmp_out"

# Each algorithm must get a fresh copy of the workload; if the copy were shared,
# the second algorithm would see remaining == 0 everywhere and report nonsense.
ok "compare gives every algorithm the same total time" \
   "1" \
   "$($SCHED_BIN compare tests/workload1.txt |
        awk '$2 ~ /^[0-9]+\.[0-9][0-9]$/ {print $NF}' | sort -u | wc -l)"

echo
echo "=== CSV output ==="

csv_out=$($SCHED_BIN sjf --csv tests/workload1.txt)

contains "CSV starts with a header row" \
         "algorithm,process,arrival,burst,priority,start,finish,turnaround,waiting,response" \
         "$csv_out"
contains "CSV has one row per process" "SJF,P4,3,2,0,8,10,7,5,5" "$csv_out"
ok "CSV mode prints no chart" \
   "5" \
   "$($SCHED_BIN sjf --csv tests/workload1.txt | wc -l)"
contains "compare --csv covers every algorithm" "MLFQ,P1," \
   "$($SCHED_BIN compare --csv tests/workload1.txt)"
ok "--quiet suppresses the Gantt chart" \
   "0" \
   "$($SCHED_BIN fcfs --quiet tests/workload1.txt | grep -c 'Gantt')"

echo
echo "=== command line validation ==="

ok "an unknown algorithm exits with 2" \
   "2" \
   "$($SCHED_BIN nosuchalgo > /dev/null 2>&1; echo $?)"
ok "no algorithm at all exits with 2" \
   "2" \
   "$($SCHED_BIN > /dev/null 2>&1; echo $?)"
ok "a quantum of 0 exits with 2" \
   "2" \
   "$($SCHED_BIN rr -q 0 > /dev/null 2>&1; echo $?)"
ok "-q with no number exits with 2" \
   "2" \
   "$($SCHED_BIN rr -q > /dev/null 2>&1; echo $?)"
ok "an unknown option exits with 2" \
   "2" \
   "$($SCHED_BIN fcfs --bogus > /dev/null 2>&1; echo $?)"

echo
echo "=== scheduler input validation ==="

ok "a missing workload file exits with 1" \
   "1" \
   "$($SCHED_BIN fcfs "$work/nope.txt" > /dev/null 2>&1; echo $?)"

printf 'P1 0\n' > "$work/bad1.txt"
contains "a line with too few fields is reported" \
   "expected \"name arrival burst [priority]\"" \
   "$($SCHED_BIN fcfs "$work/bad1.txt" 2>&1)"

printf 'P1 0 0\n' > "$work/bad2.txt"
contains "a burst of zero is refused" \
   "burst must be > 0" \
   "$($SCHED_BIN mlfq "$work/bad2.txt" 2>&1)"

printf 'P1 -3 5\n' > "$work/bad3.txt"
contains "a negative arrival time is refused" \
   "arrival must be >= 0" \
   "$($SCHED_BIN fcfs "$work/bad3.txt" 2>&1)"

printf 'P1 0 5 -2\n' > "$work/bad5.txt"
contains "a negative priority is refused" \
   "priority must be >= 0" \
   "$($SCHED_BIN priority "$work/bad5.txt" 2>&1)"

printf 'P1 0 5 1\n' > "$work/ok4.txt"
contains "the optional priority column is accepted" \
   "average turnaround time : 5.00" \
   "$($SCHED_BIN priority "$work/ok4.txt")"

printf '# only a comment\n' > "$work/bad4.txt"
contains "a workload with no processes is refused" \
   "contains no processes" \
   "$($SCHED_BIN fcfs "$work/bad4.txt" 2>&1)"

contains "a workload too long for the time limit is refused" \
   "but the simulator is built for" \
   "$(printf 'X 0 400\nY 1 400\n' | $SCHED_BIN fcfs - 2>&1)"

ok "an over-long workload exits with 1 instead of printing nonsense" \
   "1" \
   "$(printf 'X 0 400\nY 1 400\n' | $SCHED_BIN fcfs - > /dev/null 2>&1; echo $?)"

ok "--help exits with 0" \
   "0" \
   "$($SCHED_BIN --help > /dev/null 2>&1; echo $?)"

ok "too many arguments exits with 2" \
   "2" \
   "$($SCHED_BIN fcfs a b > /dev/null 2>&1; echo $?)"

contains "the workload can be read from standard input" \
   "average turnaround time : 1.00" \
   "$(printf 'ONE 0 1\n' | $SCHED_BIN fcfs -)"

echo
echo "-------------------------------------"
printf 'passed: %d   failed: %d\n' "$passed" "$failed"
echo "-------------------------------------"

[ "$failed" -eq 0 ]
