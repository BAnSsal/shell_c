/*
 * algo_priority.c - Priority scheduling (non-preemptive)
 * -----------------------------------------------------
 *
 * Out of everything that has arrived, run the most important process, where
 * "most important" means the SMALLEST priority number (priority 1 beats
 * priority 3). Once it starts it runs to completion.
 *
 * The priority comes from the optional fourth column of the workload file:
 *
 *      # name  arrival  burst  priority
 *      HIGH    2        3      1
 *      BG      3        5      4
 *
 * If a workload has no priority column, every process gets priority 0 and this
 * algorithm behaves exactly like FCFS - which is a useful thing to see.
 *
 * The famous problem: **starvation**. A low-priority process can be skipped
 * forever if important work keeps arriving. The classic fix is *aging* -
 * slowly improve the priority of anything that has been waiting too long -
 * which is the same insight as MLFQ's periodic priority boost.
 */

#include "scheduler.h"

/*
 * Which ready process is the most important?
 *
 * Ties are broken by arrival time and then by position in the file, so two
 * processes with the same priority are served first-come-first-served and the
 * result is reproducible.
 */
static int pick_most_important(const struct process procs[], int n, int time)
{
    int best = -1;

    for (int i = 0; i < n; i++) {
        if (procs[i].arrival > time || procs[i].remaining == 0)
            continue;
        if (best < 0 ||
            procs[i].priority < procs[best].priority ||
            (procs[i].priority == procs[best].priority &&
             procs[i].arrival < procs[best].arrival))
            best = i;
    }
    return best;
}

int run_priority(struct process procs[], int n, struct segment segs[])
{
    int nsegs     = 0;
    int time      = 0;
    int completed = 0;

    while (completed < n && time < MAX_TIME) {
        int pick = pick_most_important(procs, n, time);

        if (pick < 0) {                         /* nothing has arrived yet */
            add_tick(segs, &nsegs, -1, time);
            time++;
            continue;
        }

        procs[pick].start_time = time;

        /* Non-preemptive: even a priority-1 process that arrives one time unit
         * from now has to wait for this one to finish. */
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
