/*
 * mlfq.c - Multi-Level Feedback Queue scheduling
 * ----------------------------------------------
 *
 * MLFQ is the scheduler idea real systems actually use. It does not know in
 * advance which process is short and which is long, so it *learns* by watching
 * how each process behaves.
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
 *   R4  If a process is still in a queue when a process arrives in a HIGHER
 *       queue, it is preempted and the new one runs.
 *   R5  Every BOOST_INTERVAL time units, everything is moved back to Q0.
 *       Without this rule a long job that got demoted to Q2 could starve
 *       forever while short jobs keep arriving.
 *
 * The result: short processes finish inside their first slice and never get
 * demoted, so they respond quickly; long processes sink to the bottom queue
 * and share the leftover CPU time.
 *
 * Build:  gcc -Wall -Wextra -std=c11 -o bin/mlfq src/mlfq.c src/scheduler_common.c
 * Run:    ./bin/mlfq tests/workload1.txt
 */

#include "scheduler.h"

#include <stdio.h>
#include <string.h>

#define NQUEUES 3           /* how many priority levels                     */
#define BOOST_INTERVAL 15   /* move everything back to Q0 this often; 0 = off */

/* The time slice of each queue. Lower priority means a longer slice, because a
 * long job then wastes less time on context switches. */
static const int quantum[NQUEUES] = {2, 4, 8};

/* ======================================================================== */
/* A tiny FIFO queue of process indexes                                     */
/* ======================================================================== */

/*
 * A circular buffer. `head` is where the next pop comes from and `count` is
 * how many entries are stored, so the free slot for a push is
 * (head + count) % MAX_PROCS. A process is only ever in one queue at a time,
 * so MAX_PROCS slots are always enough.
 */
struct queue {
    int items[MAX_PROCS];
    int head;
    int count;
};

static void q_init(struct queue *q)
{
    q->head  = 0;
    q->count = 0;
}

static int q_empty(const struct queue *q) { return q->count == 0; }

static void q_push(struct queue *q, int value)
{
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
 * whole time slice at once) is what lets a process that arrives in the middle
 * of a slice preempt the running one.
 *
 * Returns the number of recorded segments.
 */
static int run_mlfq(struct process procs[], int n, struct segment segs[])
{
    struct queue queues[NQUEUES];
    int          nsegs     = 0;
    int          time      = 0;
    int          completed = 0;
    int          running   = -1;     /* index of the process on the CPU     */
    int          used      = 0;      /* how much of its slice it has used   */

    for (int q = 0; q < NQUEUES; q++)
        q_init(&queues[q]);

    printf("Scheduling events\n");

    while (completed < n && time < MAX_TIME) {

        /* --- R1: admit every process that arrives at this instant --- */
        for (int i = 0; i < n; i++) {
            if (procs[i].arrival == time) {
                procs[i].queue = 0;
                q_push(&queues[0], i);
                printf("  t=%-3d %s arrives, joins Q0\n", time, procs[i].name);
            }
        }

        /* --- R5: periodic priority boost --- */
        if (BOOST_INTERVAL > 0 && time > 0 && time % BOOST_INTERVAL == 0) {
            int moved = 0;

            /* Empty every queue below Q0 and refill Q0 with what was in them.
             * Going from high to low keeps the relative order sensible. */
            for (int q = 1; q < NQUEUES; q++) {
                while (!q_empty(&queues[q])) {
                    int i = q_pop(&queues[q]);
                    procs[i].queue = 0;
                    q_push(&queues[0], i);
                    moved++;
                }
            }
            if (running >= 0 && procs[running].queue != 0) {
                procs[running].queue = 0;
                used = 0;               /* it gets a fresh Q0 slice */
                moved++;
            }
            if (moved > 0)
                printf("  t=%-3d priority boost: %d process(es) moved back "
                       "to Q0\n", time, moved);
        }

        /* --- R4: a process in a higher queue preempts the running one --- */
        if (running >= 0) {
            for (int q = 0; q < procs[running].queue; q++) {
                if (!q_empty(&queues[q])) {
                    printf("  t=%-3d %s preempted (something arrived in Q%d)\n",
                           time, procs[running].name, q);
                    q_push(&queues[procs[running].queue], running);
                    running = -1;
                    used    = 0;
                    break;
                }
            }
        }

        /* --- R2: pick a process from the highest non-empty queue --- */
        if (running < 0) {
            for (int q = 0; q < NQUEUES; q++) {
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

        /* --- did it finish? --- */
        if (procs[running].remaining == 0) {
            procs[running].finish_time = time;
            completed++;
            printf("  t=%-3d %s finished (was in Q%d)\n", time,
                   procs[running].name, procs[running].queue);
            running = -1;
            used    = 0;
            continue;
        }

        /* --- R3: used the whole slice, so demote it --- */
        if (used == quantum[procs[running].queue]) {
            int old = procs[running].queue;
            int next_q = (old + 1 < NQUEUES) ? old + 1 : old;

            procs[running].queue = next_q;
            q_push(&queues[next_q], running);

            if (next_q != old)
                printf("  t=%-3d %s used its %d-unit slice in Q%d -> demoted "
                       "to Q%d\n", time, procs[running].name, quantum[old],
                       old, next_q);
            else
                printf("  t=%-3d %s used its %d-unit slice, stays in Q%d "
                       "(lowest queue, round robin)\n", time,
                       procs[running].name, quantum[old], old);

            running = -1;
            used    = 0;
        }
    }

    if (completed < n)
        fprintf(stderr, "warning: simulation stopped at the %d time unit "
                        "limit with %d process(es) unfinished\n",
                MAX_TIME, n - completed);

    printf("\n");
    return nsegs;
}

static void usage(const char *prog)
{
    printf("usage: %s [workload-file]\n\n", prog);
    printf("Simulates Multi-Level Feedback Queue (MLFQ) CPU scheduling with\n");
    printf("%d queues (time slices", NQUEUES);
    for (int q = 0; q < NQUEUES; q++)
        printf(" %d", quantum[q]);
    printf(") and a priority boost every %d time units.\n", BOOST_INTERVAL);
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
            return 1;
    } else {
        n = load_demo_workload(procs);
        printf("(no workload file given, using the built-in example)\n\n");
    }

    printf("=== MLFQ - Multi-Level Feedback Queue ===\n\n");
    printf("Configuration\n");
    for (int q = 0; q < NQUEUES; q++)
        printf("  Q%d  time slice %d%s\n", q, quantum[q],
               q == 0 ? "   (new processes start here)" :
               q == NQUEUES - 1 ? "   (lowest priority, round robin)" : "");
    if (BOOST_INTERVAL > 0)
        printf("  priority boost: every %d time units everything returns "
               "to Q0\n", BOOST_INTERVAL);
    printf("\n");

    print_input(procs, n);

    nsegs = run_mlfq(procs, n, segs);

    print_gantt(segs, nsegs, procs, n);
    print_timeline(segs, nsegs, procs, n);
    print_summary("MLFQ", segs, nsegs, procs, n);

    printf("Note: short processes finish inside their first slice and keep a\n");
    printf("      high priority, so their response time stays small. Long\n");
    printf("      processes are demoted and share the leftover CPU time.\n");
    return 0;
}
