/*
 * fcfs.c - First Come First Served scheduling
 * -------------------------------------------
 *
 * The simplest scheduler that exists: whoever asked first goes first, and
 * nobody is ever interrupted (FCFS is *non-preemptive*).
 *
 * The algorithm in three lines:
 *
 *   1. sort the processes by arrival time
 *   2. if the next process has not arrived yet, the CPU sits idle
 *   3. run it from start to finish, then move on to the next one
 *
 * What this program shows: FCFS is fair in the "queue at a shop" sense, but a
 * single long process at the front makes everybody behind it wait. That is the
 * convoy effect, and it is the reason MLFQ exists.
 *
 * Build:  gcc -Wall -Wextra -std=c11 -o bin/fcfs src/fcfs.c src/scheduler_common.c
 * Run:    ./bin/fcfs tests/workload1.txt
 */

#include "scheduler.h"

#include <stdio.h>
#include <string.h>

/*
 * Sort the processes so that the earliest arrival comes first.
 *
 * This is a selection sort: walk the array, find the smallest remaining
 * element, swap it into place. With at most 16 processes its speed does not
 * matter at all, and it is much easier to read than qsort() with a comparison
 * function.
 *
 * When two processes arrive at the same time we keep the order they had in the
 * file, which is what "first come first served" means for a tie.
 */
static void sort_by_arrival(struct process procs[], int n)
{
    for (int i = 0; i < n - 1; i++) {
        int min = i;

        for (int j = i + 1; j < n; j++)
            if (procs[j].arrival < procs[min].arrival)
                min = j;

        if (min != i) {
            struct process tmp = procs[i];
            procs[i]   = procs[min];
            procs[min] = tmp;
        }
    }
}

/*
 * Run the simulation.
 *
 * `time` is the clock. It only ever moves forward: either because the CPU ran
 * a process for one time unit, or because it had to wait for the next arrival.
 *
 * Returns the number of recorded segments.
 */
static int run_fcfs(struct process procs[], int n, struct segment segs[])
{
    int nsegs = 0;
    int time  = 0;

    for (int i = 0; i < n; i++) {
        /* The CPU has nothing to do until this process arrives. Those idle
         * time units are recorded as well, so the Gantt chart shows the gap
         * instead of quietly skipping over it. */
        while (time < procs[i].arrival) {
            add_tick(segs, &nsegs, -1, time);
            time++;
        }

        procs[i].start_time = time;      /* it gets the CPU right now      */

        /* Non-preemptive: run the whole burst, one time unit at a time.
         * (One tick at a time is not necessary for FCFS, but it keeps this
         * loop looking like the MLFQ one, where it *is* necessary.) */
        for (int k = 0; k < procs[i].burst; k++) {
            add_tick(segs, &nsegs, i, time);
            time++;
            procs[i].remaining--;
        }

        procs[i].finish_time = time;     /* done, next process please      */
    }
    return nsegs;
}

static void usage(const char *prog)
{
    printf("usage: %s [workload-file]\n\n", prog);
    printf("Simulates First Come First Served (FCFS) CPU scheduling.\n");
    printf("With no file, a small built-in workload is used.\n");
    printf("Use '-' to read the workload from standard input.\n\n");
    printf("workload file format (one process per line):\n");
    printf("  # name  arrival  burst\n");
    printf("  P1      0        5\n");
    printf("  P2      2        3\n");
}

int main(int argc, char *argv[])
{
    struct process procs[MAX_PROCS];
    struct segment segs[MAX_SEGMENTS];
    int            n, nsegs;

    if (argc > 2) {
        fprintf(stderr, "error: too many arguments\n");
        usage(argv[0]);
        return 2;
    }
    if (argc == 2 && (strcmp(argv[1], "-h") == 0 ||
                      strcmp(argv[1], "--help") == 0)) {
        usage(argv[0]);
        return 0;
    }

    if (argc == 2) {
        n = load_workload(argv[1], procs, MAX_PROCS);
        if (n < 0)
            return 1;               /* the loader already explained why */
    } else {
        n = load_demo_workload(procs);
        printf("(no workload file given, using the built-in example)\n\n");
    }

    printf("=== FCFS - First Come First Served ===\n\n");
    print_input(procs, n);

    sort_by_arrival(procs, n);
    nsegs = run_fcfs(procs, n, segs);

    print_gantt(segs, nsegs, procs, n);
    print_timeline(segs, nsegs, procs, n);
    print_summary("FCFS", segs, nsegs, procs, n);

    printf("Note: FCFS never interrupts a running process, so a long process\n");
    printf("      at the front makes every later process wait. Compare the\n");
    printf("      waiting times with ./bin/mlfq on the same workload.\n");
    return 0;
}
