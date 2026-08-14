# Makefile - builds the three programs of this project
#
#   make          build everything into bin/
#   make run-shell        start the shell
#   make run-fcfs         run the FCFS simulation on tests/workload1.txt
#   make run-mlfq         run the MLFQ simulation on tests/workload1.txt
#   make test             run the automated test script
#   make clean            delete bin/
#
# The flags, one at a time:
#   -Wall -Wextra  turn on the warnings that catch real bugs
#   -std=c11       compile against the 2011 C standard
#   -g             keep debug information, so gdb and valgrind are useful
CC      = gcc
CFLAGS  = -Wall -Wextra -std=c11 -g

BIN     = bin

all: $(BIN)/mysh $(BIN)/fcfs $(BIN)/mlfq

# The shell is one self-contained file.
$(BIN)/mysh: src/shell.c | $(BIN)
	$(CC) $(CFLAGS) -o $@ src/shell.c

# Both schedulers link against the shared printing/loading code.
$(BIN)/fcfs: src/fcfs.c src/scheduler_common.c src/scheduler.h | $(BIN)
	$(CC) $(CFLAGS) -o $@ src/fcfs.c src/scheduler_common.c

$(BIN)/mlfq: src/mlfq.c src/scheduler_common.c src/scheduler.h | $(BIN)
	$(CC) $(CFLAGS) -o $@ src/mlfq.c src/scheduler_common.c

# The "| $(BIN)" above is an order-only prerequisite: make sure the directory
# exists first, but do not rebuild the programs when its timestamp changes.
$(BIN):
	mkdir -p $(BIN)

run-shell: $(BIN)/mysh
	./$(BIN)/mysh

run-fcfs: $(BIN)/fcfs
	./$(BIN)/fcfs tests/workload1.txt

run-mlfq: $(BIN)/mlfq
	./$(BIN)/mlfq tests/workload1.txt

test: all
	./tests/run_tests.sh

clean:
	rm -rf $(BIN)

.PHONY: all run-shell run-fcfs run-mlfq test clean
