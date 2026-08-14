/*
 * algo_fcfs.c - First Come First Served
 * -------------------------------------
 *
 * The simplest scheduler that exists: whoever asked first goes first, and
 * nobody is ever interrupted (FCFS is *non-preemptive*).
 *
 *   1. sort the processes by arrival time
 *   2. if the next process has not arrived yet, the CPU sits idle
 *   3. run it from start to finish, then move on to the next one
 *
 * Weakness: one long process at the front makes everybody behind it wait.
 * That is the "convoy effect", and it is the reason all the other algorithms
 * in this project exist.
 */

#include "scheduler.h"

/*
 * Sort the processes so that the earliest arrival comes first.
 *
 * This is a selection sort: walk the array, find the smallest remaining
 * element, swap it into place. With at most 16 processes its speed does not
 * matter, and it is far easier to read than qsort() with a comparison function.
 *
 * The comparison is strictly `<`, so two processes that arrive at the same time
 * keep the order they had in the file - for a tie, "first come" can only mean
 * "first listed".
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

int run_fcfs(struct process procs[], int n, struct segment segs[])
{
    int nsegs = 0;
    int time  = 0;

    sort_by_arrival(procs, n);

    for (int i = 0; i < n; i++) {
        /* The CPU has nothing to do until this process arrives. Those idle
         * time units are recorded too, so the Gantt chart shows the gap
         * instead of quietly skipping over it. */
        while (time < procs[i].arrival) {
            add_tick(segs, &nsegs, -1, time);
            time++;
        }

        procs[i].start_time = time;      /* it gets the CPU right now */

        /* Non-preemptive: run the whole burst, one time unit at a time.
         * (One tick at a time is not necessary here - `time += burst` would
         * do - but it keeps this loop looking like the preemptive ones, where
         * it *is* necessary.) */
        for (int k = 0; k < procs[i].burst; k++) {
            add_tick(segs, &nsegs, i, time);
            time++;
            procs[i].remaining--;
        }

        procs[i].finish_time = time;     /* done, next process please */
    }
    return nsegs;
}
