/*
 * sched.c - the CPU scheduling simulator's command line
 * -----------------------------------------------------
 *
 *   ./bin/sched <algorithm> [options] [workload-file]
 *   ./bin/sched compare     [options] [workload-file]
 *
 * There is one program rather than six because everything except the choice of
 * algorithm is identical: read the workload, run it, print the Gantt chart, the
 * timeline and the metrics. The algorithms themselves live in src/algo_*.c.
 *
 * Examples:
 *   ./bin/sched fcfs tests/workload1.txt
 *   ./bin/sched rr -q 3 tests/workload1.txt
 *   ./bin/sched compare tests/workload1.txt
 *   ./bin/sched sjf --csv tests/workload1.txt > sjf.csv
 */

#include "scheduler.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEFAULT_QUANTUM 3

/* Every algorithm this program knows about, in the order `compare` runs them.
 * `label` is what appears in the output, `name` is what the user types. */
static const struct {
    const char *name;
    const char *label;
    const char *description;
} algorithms[] = {
    {"fcfs",     "FCFS",     "First Come First Served (non-preemptive)"},
    {"sjf",      "SJF",      "Shortest Job First (non-preemptive)"},
    {"srtf",     "SRTF",     "Shortest Remaining Time First (preemptive SJF)"},
    {"rr",       "RR",       "Round Robin (use -q to set the quantum)"},
    {"priority", "PRIORITY", "Priority, smallest number first (non-preemptive)"},
    {"mlfq",     "MLFQ",     "Multi-Level Feedback Queue"},
};
#define NALGOS ((int)(sizeof algorithms / sizeof algorithms[0]))

/*
 * Run one algorithm by name. Returns the number of recorded segments, or -1 if
 * the name is not one we know.
 *
 * A plain if/else chain rather than a table of function pointers: the six
 * functions do not all take the same arguments (Round Robin needs a quantum,
 * MLFQ needs the verbose flag), and this way the differences stay visible.
 */
static int run_algorithm(const char *name, struct process procs[], int n,
                         struct segment segs[], int quantum, int verbose)
{
    if (strcmp(name, "fcfs") == 0)
        return run_fcfs(procs, n, segs);
    if (strcmp(name, "sjf") == 0)
        return run_sjf(procs, n, segs);
    if (strcmp(name, "srtf") == 0)
        return run_srtf(procs, n, segs);
    if (strcmp(name, "rr") == 0)
        return run_rr(procs, n, segs, quantum);
    if (strcmp(name, "priority") == 0)
        return run_priority(procs, n, segs);
    if (strcmp(name, "mlfq") == 0)
        return run_mlfq(procs, n, segs, verbose);
    return -1;
}

static const char *label_of(const char *name)
{
    for (int i = 0; i < NALGOS; i++)
        if (strcmp(algorithms[i].name, name) == 0)
            return algorithms[i].label;
    return name;
}

static const char *description_of(const char *name)
{
    for (int i = 0; i < NALGOS; i++)
        if (strcmp(algorithms[i].name, name) == 0)
            return algorithms[i].description;
    return "";
}

static int is_known_algorithm(const char *name)
{
    for (int i = 0; i < NALGOS; i++)
        if (strcmp(algorithms[i].name, name) == 0)
            return 1;
    return 0;
}

static void usage(const char *prog)
{
    printf("usage: %s <algorithm> [options] [workload-file]\n", prog);
    printf("       %s compare [options] [workload-file]\n\n", prog);

    printf("algorithms\n");
    for (int i = 0; i < NALGOS; i++)
        printf("  %-10s %s\n", algorithms[i].name, algorithms[i].description);
    printf("  %-10s run every algorithm above and print a comparison table\n",
           "compare");
    printf("\n");

    printf("options\n");
    printf("  -q N       Round Robin quantum (default %d)\n", DEFAULT_QUANTUM);
    printf("  --csv      print the results as CSV, for a spreadsheet or gnuplot\n");
    printf("  --quiet    only print the metrics (no chart, no timeline)\n");
    printf("  -h, --help show this text\n");
    printf("\n");

    printf("workload file format (one process per line)\n");
    printf("  # name  arrival  burst  [priority]\n");
    printf("  P1      0        5      2\n");
    printf("  P2      2        3      1\n");
    printf("  Blank lines and lines starting with '#' are ignored.\n");
    printf("  The priority column is optional and defaults to 0.\n");
    printf("  Use '-' as the file name to read the workload from stdin,\n");
    printf("  or give no file at all to use the built-in example.\n\n");

    printf("MLFQ configuration (compiled in, see src/algo_mlfq.c)\n");
    printf("  %d queues with time slices", MLFQ_QUEUES);
    for (int q = 0; q < MLFQ_QUEUES; q++)
        printf(" %d", mlfq_quantum[q]);
    printf(", priority boost every %d time units\n", MLFQ_BOOST_INTERVAL);
}

/* ------------------------------------------------------------------------ */
/* compare: run every algorithm on the same workload                        */
/* ------------------------------------------------------------------------ */

/*
 * Each algorithm gets its own COPY of the workload.
 *
 * Two reasons this matters: the algorithms overwrite remaining/start/finish
 * while they run, and FCFS even sorts the array. Handing the next algorithm a
 * used-up workload would produce quietly wrong numbers, which is the sort of
 * bug that is very hard to spot in a table of plausible-looking averages.
 */
static void run_comparison(const struct process original[], int n, int quantum,
                           int csv)
{
    struct metrics results[NALGOS];

    for (int a = 0; a < NALGOS; a++) {
        struct process work[MAX_PROCS];
        struct segment segs[MAX_SEGMENTS];
        int            nsegs;

        memcpy(work, original, (size_t)n * sizeof *work);
        reset_processes(work, n);

        nsegs = run_algorithm(algorithms[a].name, work, n, segs, quantum, 0);
        results[a] = compute_metrics(segs, nsegs, work, n);

        if (csv)
            print_csv(algorithms[a].label, work, n, a == 0);
    }

    if (csv)
        return;

    printf("Comparison (same workload, every algorithm)\n");
    printf("  %-10s %15s %13s %13s %10s %11s\n", "algorithm", "avg turnaround",
           "avg waiting", "avg response", "switches", "total time");
    for (int a = 0; a < NALGOS; a++)
        printf("  %-10s %15.2f %13.2f %13.2f %10d %11d\n",
               algorithms[a].label, results[a].avg_turnaround,
               results[a].avg_waiting, results[a].avg_response,
               results[a].switches, results[a].total_time);
    printf("\n");

    /* Point at the winner of each column instead of leaving the reader to scan
     * the table. Lower is better for all three. */
    int best_t = 0, best_w = 0, best_r = 0, fewest_switches = 0;
    for (int a = 1; a < NALGOS; a++) {
        if (results[a].avg_turnaround < results[best_t].avg_turnaround) best_t = a;
        if (results[a].avg_waiting    < results[best_w].avg_waiting)    best_w = a;
        if (results[a].avg_response   < results[best_r].avg_response)   best_r = a;
        if (results[a].switches < results[fewest_switches].switches)
            fewest_switches = a;
    }
    printf("  best average turnaround : %s (%.2f)\n",
           algorithms[best_t].label, results[best_t].avg_turnaround);
    printf("  best average waiting    : %s (%.2f)\n",
           algorithms[best_w].label, results[best_w].avg_waiting);
    printf("  best average response   : %s (%.2f)\n",
           algorithms[best_r].label, results[best_r].avg_response);
    printf("  fewest context switches : %s (%d)\n",
           algorithms[fewest_switches].label, results[fewest_switches].switches);
    printf("\n");
    printf("  SRTF is the theoretical best for waiting time on one CPU, but it\n");
    printf("  needs to know the burst lengths in advance. MLFQ gets close to it\n");
    printf("  on response time while only watching how processes behave.\n");
}

/* ------------------------------------------------------------------------ */

int main(int argc, char *argv[])
{
    struct process procs[MAX_PROCS];
    struct segment segs[MAX_SEGMENTS];
    const char    *algorithm = NULL;
    const char    *file      = NULL;
    int            quantum   = DEFAULT_QUANTUM;
    int            csv = 0, quiet = 0;
    int            n, nsegs;

    /* ---- read the command line ---- */
    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];

        if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            usage(argv[0]);
            return 0;
        }
        if (strcmp(arg, "--csv") == 0) {
            csv = 1;
            continue;
        }
        if (strcmp(arg, "--quiet") == 0) {
            quiet = 1;
            continue;
        }
        if (strcmp(arg, "-q") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: -q needs a number\n");
                return 2;
            }
            quantum = atoi(argv[++i]);
            if (quantum < 1) {
                fprintf(stderr, "error: the quantum must be at least 1 "
                                "(got '%s')\n", argv[i]);
                return 2;
            }
            continue;
        }
        if (arg[0] == '-' && arg[1] != '\0' && strcmp(arg, "-") != 0) {
            fprintf(stderr, "error: unknown option '%s'\n", arg);
            usage(argv[0]);
            return 2;
        }
        /* The first non-option word is the algorithm, the second is the file. */
        if (algorithm == NULL)
            algorithm = arg;
        else if (file == NULL)
            file = arg;
        else {
            fprintf(stderr, "error: unexpected extra argument '%s'\n", arg);
            return 2;
        }
    }

    if (algorithm == NULL) {
        fprintf(stderr, "error: no algorithm given\n\n");
        usage(argv[0]);
        return 2;
    }
    if (strcmp(algorithm, "compare") != 0 && !is_known_algorithm(algorithm)) {
        fprintf(stderr, "error: unknown algorithm '%s'\n\n", algorithm);
        usage(argv[0]);
        return 2;
    }

    /* ---- load the workload ---- */
    if (file != NULL) {
        n = load_workload(file, procs, MAX_PROCS);
        if (n < 0)
            return 1;               /* the loader already explained why */
    } else {
        n = load_demo_workload(procs);
        if (!csv)
            printf("(no workload file given, using the built-in example)\n\n");
    }

    /* Refuse a workload that cannot finish inside the simulator's time limit,
     * instead of printing half a simulation with negative waiting times. */
    if (!workload_fits(procs, n))
        return 1;

    /* ---- compare mode ---- */
    if (strcmp(algorithm, "compare") == 0) {
        if (!csv) {
            printf("=== Comparing %d scheduling algorithms ===\n\n", NALGOS);
            print_input(procs, n);
            printf("Round Robin quantum: %d\n\n", quantum);
        }
        run_comparison(procs, n, quantum, csv);
        return 0;
    }

    /* ---- single algorithm ---- */
    if (!csv) {
        printf("=== %s - %s ===\n\n", label_of(algorithm),
               description_of(algorithm));
        print_input(procs, n);
        if (strcmp(algorithm, "rr") == 0)
            printf("Quantum: %d time units\n\n", quantum);
    }

    nsegs = run_algorithm(algorithm, procs, n, segs, quantum, !csv && !quiet);

    if (csv) {
        print_csv(label_of(algorithm), procs, n, 1);
        return 0;
    }

    if (!quiet) {
        print_gantt(segs, nsegs, procs, n);
        print_timeline(segs, nsegs, procs, n);
    }
    print_summary(label_of(algorithm), segs, nsegs, procs, n);
    return 0;
}
