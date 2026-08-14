/*
 * algo_srtf.c - Shortest Remaining Time First (preemptive SJF)
 * -----------------------------------------------------------
 *
 * Same idea as SJF, but the decision is re-made at *every* time unit and uses
 * the time each process has *left* rather than its original burst. So a short
 * process that arrives in the middle of a long one throws it off the CPU
 * immediately.
 *
 * SRTF gives the lowest possible average waiting time of any scheduler on a
 * single CPU. It is the theoretical best case, which makes it the useful
 * yardstick to compare everything else against.
 *
 * Two reasons no real OS uses it: it needs to know burst lengths in advance,
 * and it switches constantly (each switch costs real time in a real kernel).
 */

#include "scheduler.h"

/*
 * Which ready process has the least work left?
 *
 * `current` is whoever is running right now, or -1. It is used as the starting
 * candidate so that ties are resolved in favour of "keep going": we only switch
 * when somebody is *strictly* shorter. Without that rule a tie would make the
 * CPU swap between two equal processes every single time unit, inflating the
 * context-switch count for no benefit.
 */
static int pick_shortest_remaining(const struct process procs[], int n,
                                   int time, int current)
{
    int best = -1;

    if (current >= 0 && procs[current].remaining > 0)
        best = current;

    for (int i = 0; i < n; i++) {
        if (procs[i].arrival > time || procs[i].remaining == 0)
            continue;
        if (best < 0 || procs[i].remaining < procs[best].remaining)
            best = i;
    }
    return best;
}

int run_srtf(struct process procs[], int n, struct segment segs[])
{
    int nsegs     = 0;
    int time      = 0;
    int completed = 0;
    int running   = -1;

    while (completed < n && time < MAX_TIME) {
        running = pick_shortest_remaining(procs, n, time, running);

        if (running < 0) {                      /* nothing ready: idle tick */
            add_tick(segs, &nsegs, -1, time);
            time++;
            continue;
        }

        if (procs[running].start_time < 0)
            procs[running].start_time = time;   /* its first turn on the CPU */

        /* Exactly one time unit, then think again. That single tick is the
         * whole difference between SJF and SRTF. */
        add_tick(segs, &nsegs, running, time);
        procs[running].remaining--;
        time++;

        if (procs[running].remaining == 0) {
            procs[running].finish_time = time;
            completed++;
            running = -1;
        }
    }
    return nsegs;
}
