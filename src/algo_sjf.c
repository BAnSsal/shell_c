/*
 * algo_sjf.c - Shortest Job First (non-preemptive)
 * ------------------------------------------------
 *
 * Out of everything that has already arrived, run the process with the smallest
 * total burst. Once a process starts it is never interrupted.
 *
 * Why it matters: SJF gives the best possible average waiting time among
 * non-preemptive schedulers. Why it is not what real systems use: it needs to
 * know the burst length in advance, and nobody does. A real kernel can only
 * guess, which is exactly the guess MLFQ replaces with observed behaviour.
 *
 * Danger: a long process can be pushed back again and again if short ones keep
 * arriving. That is starvation, and plain SJF has no defence against it.
 */

#include "scheduler.h"

/*
 * Which ready process has the smallest burst?
 *
 * "Ready" means it has arrived (arrival <= time) and still has work left
 * (remaining > 0). Returns -1 when nothing is ready, which means the CPU has to
 * idle for a time unit.
 *
 * Ties are broken by arrival time, and then by position in the file, so the
 * result is always the same for the same input - a simulation that produced
 * different answers on different runs would be useless for comparison.
 */
static int pick_shortest(const struct process procs[], int n, int time)
{
    int best = -1;

    for (int i = 0; i < n; i++) {
        if (procs[i].arrival > time || procs[i].remaining == 0)
            continue;
        if (best < 0 ||
            procs[i].burst < procs[best].burst ||
            (procs[i].burst == procs[best].burst &&
             procs[i].arrival < procs[best].arrival))
            best = i;
    }
    return best;
}

int run_sjf(struct process procs[], int n, struct segment segs[])
{
    int nsegs     = 0;
    int time      = 0;
    int completed = 0;

    while (completed < n && time < MAX_TIME) {
        int pick = pick_shortest(procs, n, time);

        if (pick < 0) {                         /* nothing has arrived yet */
            add_tick(segs, &nsegs, -1, time);
            time++;
            continue;
        }

        procs[pick].start_time = time;

        /* Non-preemptive: run it to completion. Processes arriving during this
         * burst simply wait - they are picked up by the next pick_shortest(). */
        while (procs[pick].remaining > 0) {
            add_tick(segs, &nsegs, pick, time);
            procs[pick].remaining--;
            time++;
        }

        procs[pick].finish_time = time;
        completed++;
    }
    return nsegs;
}
