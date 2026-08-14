/*
 * scheduler.h - the pieces that FCFS and MLFQ share
 * -------------------------------------------------
 *
 * Both scheduling programs need the same three things:
 *
 *   1. a way to describe a process        -> struct process
 *   2. a way to record who ran when       -> struct segment
 *   3. the same printing of results       -> print_* functions
 *
 * Keeping them here means fcfs.c and mlfq.c contain only the scheduling
 * algorithm itself, which makes the two easy to compare side by side.
 */
#ifndef SCHEDULER_H
#define SCHEDULER_H

#define MAX_PROCS 16        /* processes in one workload                 */
#define MAX_SEGMENTS 1024   /* CPU slices we can record                  */
#define MAX_TIME 512        /* longest simulation we will print          */
#define NAME_LEN 12         /* room for names like "P1" or "compiler"    */

/*
 * One process. `arrival` and `burst` come from the workload file, everything
 * else is filled in by the simulation.
 *
 *   arrival     the time unit at which the process shows up
 *   burst       how many time units of CPU it needs in total
 *   remaining   how much CPU it still needs (only MLFQ changes this)
 *   queue       which MLFQ queue it currently sits in (0 = highest)
 *   start_time  first time it ever got the CPU (-1 until then)
 *   finish_time the time unit at which it completed
 */
struct process {
    char name[NAME_LEN];
    int  arrival;
    int  burst;
    int  remaining;
    int  queue;
    int  start_time;
    int  finish_time;
};

/*
 * "Process P ran from `start` until `end`".
 * `proc` is an index into the process array, or -1 to mean the CPU was idle.
 * Consecutive time units belonging to the same process are merged into one
 * segment, which is what makes the Gantt chart readable.
 */
struct segment {
    int proc;
    int start;
    int end;
};

/* --- reading the workload ------------------------------------------------ */

/*
 * Read a workload file. Each useful line is "name arrival burst", blank lines
 * and lines starting with '#' are ignored:
 *
 *      # name  arrival  burst
 *      P1      0        5
 *      P2      2        3
 *
 * Returns the number of processes, or -1 after printing an error message.
 */
int load_workload(const char *path, struct process procs[], int max);

/* A small built-in workload, used when no file is given on the command line. */
int load_demo_workload(struct process procs[]);

/* --- printing the results ----------------------------------------------- */

/* The input, echoed back so the reader knows what was simulated. */
void print_input(const struct process procs[], int n);

/*
 * A Gantt chart of the whole run:
 *
 *   +------+------+------+
 *   |  P1  | idle |  P2  |
 *   +------+------+------+
 *   0      5      7      10
 */
void print_gantt(const struct segment segs[], int nsegs,
                 const struct process procs[], int n);

/*
 * One line per process, one character per time unit:
 *
 *   P1  ####......
 *        # = running, . = ready and waiting, space = not here yet / done
 */
void print_timeline(const struct segment segs[], int nsegs,
                    const struct process procs[], int n);

/*
 * The results table (turnaround, waiting and response time per process) plus
 * the averages, CPU utilisation, throughput and number of context switches.
 */
void print_summary(const char *algorithm, const struct segment segs[],
                   int nsegs, const struct process procs[], int n);

/* Record that `proc` used the CPU during the single time unit `t`, merging it
 * into the previous segment when possible. Used by both algorithms. */
void add_tick(struct segment segs[], int *nsegs, int proc, int t);

#endif /* SCHEDULER_H */
