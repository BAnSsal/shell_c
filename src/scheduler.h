/*
 * scheduler.h - the pieces every scheduling algorithm shares
 * ----------------------------------------------------------
 *
 * There are six algorithms in this project and they all need the same things:
 *
 *   1. a way to describe a process        -> struct process
 *   2. a way to record who ran when       -> struct segment
 *   3. the same metrics and printing      -> compute_metrics(), print_*()
 *
 * Keeping those here means each src/algo_*.c file contains almost nothing but
 * the algorithm itself, so the six can be read and compared side by side.
 */
#ifndef SCHEDULER_H
#define SCHEDULER_H

#define MAX_PROCS 16        /* processes in one workload                 */
#define MAX_SEGMENTS 1024   /* CPU slices we can record                  */
#define MAX_TIME 512        /* longest simulation we will run            */
#define NAME_LEN 12         /* room for names like "P1" or "compiler"    */

/*
 * One process. `arrival`, `burst` and `priority` come from the workload file,
 * everything else is filled in by the simulation.
 *
 *   arrival     the time unit at which the process shows up
 *   burst       how many time units of CPU it needs in total
 *   priority    only used by the priority scheduler; SMALLER = more important
 *   remaining   how much CPU it still needs (counts down to 0)
 *   queue       which MLFQ queue it currently sits in (0 = highest)
 *   start_time  the first time it ever got the CPU (-1 until then)
 *   finish_time the time unit at which it completed (-1 until then)
 */
struct process {
    char name[NAME_LEN];
    int  arrival;
    int  burst;
    int  priority;
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

/* Everything print_summary() reports, so that the comparison mode can collect
 * the same numbers without re-deriving them. */
struct metrics {
    double avg_turnaround;
    double avg_waiting;
    double avg_response;
    double utilisation;    /* percent */
    double throughput;     /* processes per time unit */
    int    total_time;
    int    switches;
    int    idle_time;
};

/* ------------------------------------------------------------------------ */
/* The six algorithms. Each one fills in start_time/finish_time for every     */
/* process, records the CPU slices into segs[], and returns how many slices   */
/* it recorded.                                                              */
/* ------------------------------------------------------------------------ */

/* First Come First Served - non-preemptive, runs in arrival order. */
int run_fcfs(struct process procs[], int n, struct segment segs[]);

/* Shortest Job First - non-preemptive, picks the smallest total burst. */
int run_sjf(struct process procs[], int n, struct segment segs[]);

/* Shortest Remaining Time First - the preemptive version of SJF. */
int run_srtf(struct process procs[], int n, struct segment segs[]);

/* Round Robin - everyone gets `quantum` time units in turn. */
int run_rr(struct process procs[], int n, struct segment segs[], int quantum);

/* Priority scheduling - non-preemptive, smallest priority number wins. */
int run_priority(struct process procs[], int n, struct segment segs[]);

/* Multi-Level Feedback Queue. `verbose` prints the event log. */
int run_mlfq(struct process procs[], int n, struct segment segs[], int verbose);

/* MLFQ configuration, exposed so `sched --help` can describe it. */
#define MLFQ_QUEUES 3
#define MLFQ_BOOST_INTERVAL 15
extern const int mlfq_quantum[MLFQ_QUEUES];

/* ------------------------------------------------------------------------ */
/* Reading the workload                                                      */
/* ------------------------------------------------------------------------ */

/*
 * Read a workload file. Each useful line is
 *
 *      name arrival burst [priority]
 *
 * and blank lines plus lines starting with '#' are ignored:
 *
 *      # name  arrival  burst  priority
 *      P1      0        5      2
 *      P2      2        3      1
 *
 * The priority column is optional and defaults to 0.
 * Returns the number of processes, or -1 after printing an error message.
 */
int load_workload(const char *path, struct process procs[], int max);

/* A small built-in workload, used when no file is given on the command line. */
int load_demo_workload(struct process procs[]);

/*
 * Will this workload finish inside the MAX_TIME limit?
 *
 * The latest any work-conserving scheduler can finish is
 * (latest arrival) + (sum of all bursts), so that upper bound is checked once,
 * before the simulation starts. Without this check a workload that is too big
 * silently produces nonsense - unfinished processes keep finish_time == -1 and
 * turn into negative waiting times in the averages.
 *
 * Returns 1 if it fits, or 0 after printing an explanation.
 */
int workload_fits(const struct process procs[], int n);

/* Put remaining/queue/start_time/finish_time back to their starting values.
 * The comparison mode needs this before handing the same workload to the next
 * algorithm. */
void reset_processes(struct process procs[], int n);

/* Record that `proc` used the CPU during the single time unit `t`, merging it
 * into the previous segment when possible. Used by every algorithm. */
void add_tick(struct segment segs[], int *nsegs, int proc, int t);

/* ------------------------------------------------------------------------ */
/* Printing the results                                                      */
/* ------------------------------------------------------------------------ */

struct metrics compute_metrics(const struct segment segs[], int nsegs,
                               const struct process procs[], int n);

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

/* The per-process results table plus the averages. */
void print_summary(const char *algorithm, const struct segment segs[],
                   int nsegs, const struct process procs[], int n);

/* Machine-readable version of the results table, for a spreadsheet or gnuplot.
 * Pass with_header = 0 to leave out the column names. */
void print_csv(const char *algorithm, const struct process procs[], int n,
               int with_header);

#endif /* SCHEDULER_H */
