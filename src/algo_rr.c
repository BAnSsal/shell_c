/*
 * algo_rr.c - Round Robin
 * -----------------------
 *
 * Everybody takes turns. Each process gets at most `quantum` time units, and if
 * it is not finished by then it goes to the back of the queue.
 *
 * Round Robin is the fairest simple scheduler and it gives short processes a
 * good response time without needing to know any burst lengths in advance.
 * The quantum is the whole trade-off:
 *
 *   quantum too small -> excellent response, but the CPU spends its life
 *                        switching instead of working
 *   quantum too large -> fewer switches, but it degenerates into FCFS
 *                        (try -q 20 on a workload whose bursts are all smaller)
 *
 * MLFQ is essentially Round Robin per queue, plus a rule for deciding which
 * queue a process deserves.
 */

#include "scheduler.h"

/*
 * The ready queue, as a circular buffer.
 *
 * `head` is where the next pop comes from and `count` is how many entries are
 * stored, so the free slot for a push is (head + count) % MAX_PROCS. The `%` is
 * what makes the array wrap around, so pushing and popping thousands of times
 * never runs off the end. A process is only ever in the queue once, so
 * MAX_PROCS slots are always enough.
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

int run_rr(struct process procs[], int n, struct segment segs[], int quantum)
{
    struct queue ready;
    int          nsegs     = 0;
    int          time      = 0;
    int          completed = 0;
    int          running   = -1;
    int          used      = 0;     /* time units used out of the quantum */

    if (quantum < 1)
        quantum = 1;

    q_init(&ready);

    while (completed < n && time < MAX_TIME) {
        /* 1. Admit everything that arrives at this instant. This happens
         *    BEFORE the quantum check below, so a process that arrives exactly
         *    when somebody's slice expires queues up ahead of it. That is the
         *    usual convention, and it changes the answer, so it is worth being
         *    deliberate about. */
        for (int i = 0; i < n; i++)
            if (procs[i].arrival == time)
                q_push(&ready, i);

        /* 2. Has the running process used up its slice? Then it goes to the
         *    back of the queue and somebody else gets a turn. */
        if (running >= 0 && used == quantum) {
            q_push(&ready, running);
            running = -1;
            used    = 0;
        }

        /* 3. Nobody on the CPU: take the next process from the front. */
        if (running < 0 && !q_empty(&ready)) {
            running = q_pop(&ready);
            used    = 0;
        }

        /* 4. Still nobody: the CPU is idle for this time unit. */
        if (running < 0) {
            add_tick(segs, &nsegs, -1, time);
            time++;
            continue;
        }

        /* 5. Run one time unit. */
        if (procs[running].start_time < 0)
            procs[running].start_time = time;

        add_tick(segs, &nsegs, running, time);
        procs[running].remaining--;
        used++;
        time++;

        if (procs[running].remaining == 0) {
            procs[running].finish_time = time;
            completed++;
            running = -1;
            used    = 0;
        }
    }
    return nsegs;
}
