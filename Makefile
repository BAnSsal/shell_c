# Makefile - builds the two programs of this project
#
#   make               build everything into bin/
#   make run-shell     start the shell
#   make run-fcfs      FCFS on tests/workload1.txt
#   make run-mlfq      MLFQ on tests/workload1.txt
#   make run-compare   all six algorithms side by side
#   make test          run the automated test script
#   make clean         delete bin/
#
# The flags, one at a time:
#   -Wall -Wextra  turn on the warnings that catch real bugs
#   -std=c11       compile against the 2011 C standard
#   -g             keep debug information, so gdb and the sanitizers are useful
CC      = gcc
CFLAGS  = -Wall -Wextra -std=c11 -g

BIN     = bin

# The scheduling simulator is built from one file per algorithm plus the shared
# printing code and the command line. Listing the pieces in a variable keeps the
# link rule short and makes it obvious where to add a seventh algorithm.
SCHED_SRC = src/sched.c src/scheduler_common.c \
            src/algo_fcfs.c src/algo_sjf.c src/algo_srtf.c \
            src/algo_rr.c src/algo_priority.c src/algo_mlfq.c

all: $(BIN)/mysh $(BIN)/sched

# The shell is one self-contained file.
$(BIN)/mysh: src/shell.c | $(BIN)
	$(CC) $(CFLAGS) -o $@ src/shell.c

$(BIN)/sched: $(SCHED_SRC) src/scheduler.h | $(BIN)
	$(CC) $(CFLAGS) -o $@ $(SCHED_SRC)

# The "| $(BIN)" above is an order-only prerequisite: make sure the directory
# exists first, but do not rebuild the programs when its timestamp changes.
$(BIN):
	mkdir -p $(BIN)

run-shell: $(BIN)/mysh
	./$(BIN)/mysh

run-fcfs: $(BIN)/sched
	./$(BIN)/sched fcfs tests/workload1.txt

run-mlfq: $(BIN)/sched
	./$(BIN)/sched mlfq tests/workload1.txt

run-compare: $(BIN)/sched
	./$(BIN)/sched compare tests/workload1.txt

test: all
	./tests/run_tests.sh

clean:
	rm -rf $(BIN)

.PHONY: all run-shell run-fcfs run-mlfq run-compare test clean
