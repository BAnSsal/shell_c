#!/bin/bash
#
# run_tests.sh - checks that the shell and the two schedulers still behave.
#
#   ./tests/run_tests.sh          (or: make test)
#
# Every test does the same three things: run something, compare the output or
# the exit code with what we expect, print PASS or FAIL. The script exits with
# status 1 if anything failed, so it can be used in a CI pipeline later.

# Run from the project root no matter where the script was called from.
cd "$(dirname "$0")/.." || exit 1

SHELL_BIN=./bin/mysh
FCFS_BIN=./bin/fcfs
MLFQ_BIN=./bin/mlfq

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

for prog in "$SHELL_BIN" "$FCFS_BIN" "$MLFQ_BIN"; do
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

fcfs_out=$($FCFS_BIN tests/workload1.txt)

contains "FCFS runs P1 first" "| P1 | P2 | P3 | P4 |" "$fcfs_out"
contains "FCFS average waiting time is 8.75" \
         "average waiting time    : 8.75" "$fcfs_out"
contains "FCFS average turnaround time is 14.50" \
         "average turnaround time : 14.50" "$fcfs_out"
contains "FCFS keeps the CPU 100% busy on this workload" \
         "CPU utilisation         : 100.0%" "$fcfs_out"

fcfs_idle=$($FCFS_BIN tests/workload3.txt)
contains "FCFS shows an idle block when nothing has arrived" "idle" "$fcfs_idle"

echo
echo "=== MLFQ ==="

mlfq_out=$($MLFQ_BIN tests/workload1.txt)

contains "MLFQ demotes a process that uses its whole slice" \
         "demoted to Q1" "$mlfq_out"
contains "MLFQ boosts priorities periodically" \
         "priority boost" "$mlfq_out"
contains "MLFQ response time (1.50) beats FCFS (8.75)" \
         "average response time   : 1.50" "$mlfq_out"

mlfq_out2=$($MLFQ_BIN tests/workload2.txt)
contains "MLFQ preempts a low-priority process when work arrives" \
         "preempted" "$mlfq_out2"
contains "MLFQ finishes the short late arrival quickly" \
         "C finished" "$mlfq_out2"

echo
echo "=== scheduler input validation ==="

ok "a missing workload file exits with 1" \
   "1" \
   "$($FCFS_BIN "$work/nope.txt" > /dev/null 2>&1; echo $?)"

printf 'P1 0\n' > "$work/bad1.txt"
contains "a line with too few fields is reported" \
   "expected \"name arrival burst\"" \
   "$($FCFS_BIN "$work/bad1.txt" 2>&1)"

printf 'P1 0 0\n' > "$work/bad2.txt"
contains "a burst of zero is refused" \
   "burst must be > 0" \
   "$($MLFQ_BIN "$work/bad2.txt" 2>&1)"

printf 'P1 -3 5\n' > "$work/bad3.txt"
contains "a negative arrival time is refused" \
   "arrival must be >= 0" \
   "$($FCFS_BIN "$work/bad3.txt" 2>&1)"

printf '# only a comment\n' > "$work/bad4.txt"
contains "a workload with no processes is refused" \
   "contains no processes" \
   "$($FCFS_BIN "$work/bad4.txt" 2>&1)"

ok "--help exits with 0" \
   "0" \
   "$($MLFQ_BIN --help > /dev/null 2>&1; echo $?)"

ok "too many arguments exits with 2" \
   "2" \
   "$($FCFS_BIN a b > /dev/null 2>&1; echo $?)"

contains "the workload can be read from standard input" \
   "average turnaround time : 1.00" \
   "$(printf 'ONE 0 1\n' | $FCFS_BIN -)"

echo
echo "-------------------------------------"
printf 'passed: %d   failed: %d\n' "$passed" "$failed"
echo "-------------------------------------"

[ "$failed" -eq 0 ]
