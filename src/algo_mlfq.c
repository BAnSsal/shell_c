/*
 * algo_mlfq.c - Multi-Level Feedback Queue
 * ----------------------------------------
 *
 * MLFQ is the idea real systems actually use. It does not know in advance which
 * process is short and which is long, so it *learns* by watching how each one
 * behaves.
 *
 * The set-up: three queues, each with its own time slice.
 *
 *      Q0  (highest priority)  time slice 2   <- new processes start here
 *      Q1                      time slice 4
 *      Q2  (lowest priority)   time slice 8   <- round robin at the bottom
 *
 * The rules:
 *
 *   R1  A new process enters the highest queue, Q0.
 *   R2  The CPU always runs a process from the highest non-empty queue.
 *       Within one queue the order is first-in first-out.
 *   R3  If a process uses up its whole time slice, it is *demoted* one level.
 *       Using a lot of CPU is the evidence that it is a long job.
 *   R4  If a process arrives in a HIGHER queue than the running one, the
 *       running process is preempted and the new one goes first.
 *   R5  Every MLFQ_BOOST_INTERVAL time units, everything is moved back to Q0.
 *       Without this rule a long job demoted to Q2 could starve forever while
 *       short jobs keep arriving.
 *
 * The result: short processes finish inside their first slice and keep a high
 * priority, so they respond quickly; long processes sink to the bottom queue and
 * share the leftover CPU time. Nobody had to declare which is which.
 */

#include "scheduler.h"

#include <stdio.h>

/* Lower priority gets a *longer* slice, because a long job then wastes less
 * time being switched in and out. */
const int mlfq_quantum[MLFQ_QUEUES] = {2, 4, 8};

/* ======================================================================== */
/* A tiny FIFO queue of process indexes (a circular buffer)                 */
/* ======================================================================== */

struct queue {
    int items[MAX_PROCS];
    int head;   /* where the next pop comes from        */
    int count;  /* how many entries are stored right now */
};

static void q_init(struct queue *q)
{
    q->head  = 0;
    q->count = 0;
}

static int q_empty(const struct queue *q) { return q->count == 0; }

static void q_push(struct queue *q, int value)
{
    /* The free slot is `count` places after the head, and % wraps it around. */
    q->items[(q->head + q->count) % MAX_PROCS] = value;
    q->count++;
}

static int q_pop(struct queue *q)
{
    int value = q->items[q->head];

    q->head = (q->head + 1) % MAX_PROCS;
    q->count--;
    return value;
}

/* ======================================================================== */
/* The simulation                                                           */
/* ======================================================================== */

/*
 * Run MLFQ one time unit at a time. Stepping tick by tick (instead of running a
 * whole slice at once) is what lets a process that arrives in the middle of a
 * slice preempt the running one.
 */
int run_mlfq(struct process procs[], int n, struct segment segs[], int verbose)
{
    struct queue queues[MLFQ_QUEUES];
    int          nsegs     = 0;
    int          time      = 0;
    int          completed = 0;
    int          running   = -1;    /* index of the process on the CPU     */
    int          used      = 0;     /* how much of its slice it has used   */

    for (int q = 0; q < MLFQ_QUEUES; q++)
        q_init(&queues[q]);

    if (verbose)
        printf("Scheduling events\n");

    while (completed < n && time < MAX_TIME) {

        /* --- R1: admit every process that arrives at this instant --- */
        for (int i = 0; i < n; i++) {
            if (procs[i].arrival == time) {
                procs[i].queue = 0;
                q_push(&queues[0], i);
                if (verbose)
                    printf("  t=%-3d %s arrives, joins Q0\n", time,
                           procs[i].name);
            }
        }

        /* --- R5: periodic priority boost --- */
        if (MLFQ_BOOST_INTERVAL > 0 && time > 0 &&
            time % MLFQ_BOOST_INTERVAL == 0) {
            int moved = 0;

            /* Empty every queue below Q0 and refill Q0 with what was in them.
             * Going from high to low keeps the relative order sensible. */
            for (int q = 1; q < MLFQ_QUEUES; q++) {
                while (!q_empty(&queues[q])) {
                    int i = q_pop(&queues[q]);
                    procs[i].queue = 0;
                    q_push(&queues[0], i);
                    moved++;
                }
            }
            /* The running process was popped out of its queue, so it has to be
             * boosted separately. */
            if (running >= 0 && procs[running].queue != 0) {
                procs[running].queue = 0;
                used = 0;               /* it gets a fresh Q0 slice */
                moved++;
            }
            if (moved > 0 && verbose)
                printf("  t=%-3d priority boost: %d process(es) moved back "
                       "to Q0\n", time, moved);
        }

        /* --- R4: a process in a higher queue preempts the running one --- */
        if (running >= 0) {
            /* Only queues ABOVE the running process are checked, so an arrival
             * at the same priority waits its turn instead of interrupting. */
            for (int q = 0; q < procs[running].queue; q++) {
                if (!q_empty(&queues[q])) {
                    if (verbose)
                        printf("  t=%-3d %s preempted (something arrived in "
                               "Q%d)\n", time, procs[running].name, q);
                    q_push(&queues[procs[running].queue], running);
                    running = -1;
                    used    = 0;
                    break;
                }
            }
        }

        /* --- R2: pick a process from the highest non-empty queue --- */
        if (running < 0) {
            for (int q = 0; q < MLFQ_QUEUES; q++) {
                if (!q_empty(&queues[q])) {
                    running = q_pop(&queues[q]);
                    used    = 0;
                    break;
                }
            }
        }

        /* --- nothing to run: the CPU is idle for this time unit --- */
        if (running < 0) {
            add_tick(segs, &nsegs, -1, time);
            time++;
            continue;
        }

        /* --- run the chosen process for exactly one time unit --- */
        if (procs[running].start_time < 0)
            procs[running].start_time = time;   /* its first turn on the CPU */

        add_tick(segs, &nsegs, running, time);
        procs[running].remaining--;
        used++;
        time++;

        /* --- did it finish? (checked before demotion, so a process that ends
         *     exactly at the end of its slice is not pointlessly demoted) --- */
        if (procs[running].remaining == 0) {
            procs[running].finish_time = time;
            completed++;
            if (verbose)
                printf("  t=%-3d %s finished (was in Q%d)\n", time,
                       procs[running].name, procs[running].queue);
            running = -1;
            used    = 0;
            continue;
        }

        /* --- R3: used the whole slice, so demote it --- */
        if (used == mlfq_quantum[procs[running].queue]) {
            int old    = procs[running].queue;
            int next_q = (old + 1 < MLFQ_QUEUES) ? old + 1 : old;

            procs[running].queue = next_q;
            q_push(&queues[next_q], running);

            if (verbose) {
                if (next_q != old)
                    printf("  t=%-3d %s used its %d-unit slice in Q%d -> "
                           "demoted to Q%d\n", time, procs[running].name,
                           mlfq_quantum[old], old, next_q);
                else
                    printf("  t=%-3d %s used its %d-unit slice, stays in Q%d "
                           "(lowest queue, round robin)\n", time,
                           procs[running].name, mlfq_quantum[old], old);
            }

            running = -1;
            used    = 0;
        }
    }

    if (completed < n)
        fprintf(stderr, "warning: simulation stopped at the %d time unit "
                        "limit with %d process(es) unfinished\n",
                MAX_TIME, n - completed);

    if (verbose)
        printf("\n");
    return nsegs;
}
