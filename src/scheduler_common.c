/*
 * scheduler_common.c - workload loading, metrics and result printing
 * ------------------------------------------------------------------
 * Shared by all six algorithms so that every one of them prints its results in
 * exactly the same shape and can be compared line by line.
 */

#include "scheduler.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

/* ======================================================================== */
/* Reading the workload                                                     */
/* ======================================================================== */

void reset_processes(struct process procs[], int n)
{
    for (int i = 0; i < n; i++) {
        procs[i].remaining   = procs[i].burst;
        procs[i].queue       = 0;   /* everyone starts in the highest queue  */
        procs[i].start_time  = -1;  /* -1 means "has not run yet"            */
        procs[i].finish_time = -1;
    }
}

int load_workload(const char *path, struct process procs[], int max)
{
    FILE *fp;
    char  line[256];
    int   n      = 0;
    int   lineno = 0;

    /* "-" is the usual Unix way of saying "read from standard input". */
    if (strcmp(path, "-") == 0) {
        fp = stdin;
    } else {
        fp = fopen(path, "r");
        if (fp == NULL) {
            fprintf(stderr, "error: cannot open workload file '%s'\n", path);
            return -1;
        }
    }

    while (fgets(line, sizeof line, fp) != NULL) {
        char name[NAME_LEN];
        int  arrival, burst;
        int  priority = 0;      /* the 4th column is optional */
        int  fields;
        int  i = 0;

        lineno++;

        while (isspace((unsigned char)line[i]))
            i++;
        if (line[i] == '\0' || line[i] == '#')
            continue;                   /* blank line or comment */

        /* %11s stops one character before the end of name[], so a very long
         * name cannot run off the end of the buffer. sscanf() returns how many
         * items it managed to convert: 3 without a priority, 4 with one. */
        fields = sscanf(line + i, "%11s %d %d %d", name, &arrival, &burst,
                        &priority);
        if (fields != 3 && fields != 4) {
            fprintf(stderr, "error: %s line %d: expected "
                            "\"name arrival burst [priority]\"\n",
                    path, lineno);
            goto fail;
        }
        if (arrival < 0 || burst <= 0) {
            fprintf(stderr, "error: %s line %d: arrival must be >= 0 and "
                            "burst must be > 0 (got %d and %d)\n",
                    path, lineno, arrival, burst);
            goto fail;
        }
        if (priority < 0) {
            fprintf(stderr, "error: %s line %d: priority must be >= 0 "
                            "(got %d)\n", path, lineno, priority);
            goto fail;
        }
        if (n == max) {
            fprintf(stderr, "error: too many processes (limit is %d)\n", max);
            goto fail;
        }

        snprintf(procs[n].name, NAME_LEN, "%s", name);
        procs[n].arrival  = arrival;
        procs[n].burst    = burst;
        procs[n].priority = priority;
        n++;
    }

    if (fp != stdin)
        fclose(fp);

    if (n == 0) {
        fprintf(stderr, "error: %s contains no processes\n", path);
        return -1;
    }
    reset_processes(procs, n);
    return n;

fail:
    if (fp != stdin)
        fclose(fp);
    return -1;
}

int load_demo_workload(struct process procs[])
{
    /* Chosen so that the algorithms behave differently: P1 is a long job that
     * arrives first, and P4 is a short one that arrives late. FCFS makes P4
     * wait for everything; SJF, SRTF and MLFQ do not. */
    static const struct {
        const char *name;
        int arrival, burst, priority;
    } demo[] = {
        {"P1", 0, 8, 2},
        {"P2", 1, 4, 1},
        {"P3", 2, 9, 3},
        {"P4", 3, 2, 1},
    };
    int n = (int)(sizeof demo / sizeof demo[0]);

    for (int i = 0; i < n; i++) {
        snprintf(procs[i].name, NAME_LEN, "%s", demo[i].name);
        procs[i].arrival  = demo[i].arrival;
        procs[i].burst    = demo[i].burst;
        procs[i].priority = demo[i].priority;
    }
    reset_processes(procs, n);
    return n;
}

int workload_fits(const struct process procs[], int n)
{
    int latest_arrival = 0;
    int total_burst    = 0;

    for (int i = 0; i < n; i++) {
        if (procs[i].arrival > latest_arrival)
            latest_arrival = procs[i].arrival;
        total_burst += procs[i].burst;
    }

    if (latest_arrival + total_burst > MAX_TIME) {
        fprintf(stderr, "error: this workload can run until time %d, but the "
                        "simulator is built for %d time units\n",
                latest_arrival + total_burst, MAX_TIME);
        fprintf(stderr, "       (raise MAX_TIME in src/scheduler.h and rebuild "
                        "if you really need this)\n");
        return 0;
    }
    return 1;
}

/* ======================================================================== */
/* Recording who ran when                                                   */
/* ======================================================================== */

void add_tick(struct segment segs[], int *nsegs, int proc, int t)
{
    /* If the same process also ran in the previous time unit, just stretch the
     * last segment. That turns 8 one-unit entries into a single "P1 0-8". */
    if (*nsegs > 0 && segs[*nsegs - 1].proc == proc &&
        segs[*nsegs - 1].end == t) {
        segs[*nsegs - 1].end = t + 1;
        return;
    }
    if (*nsegs == MAX_SEGMENTS) {
        fprintf(stderr, "warning: too many CPU slices to record, "
                        "the chart will be incomplete\n");
        return;
    }
    segs[*nsegs].proc  = proc;
    segs[*nsegs].start = t;
    segs[*nsegs].end   = t + 1;
    (*nsegs)++;
}

/* ======================================================================== */
/* Metrics                                                                  */
/* ======================================================================== */

/*
 * The three per-process numbers, for one process:
 *
 *   turnaround = finish - arrival     total time from arriving to finishing
 *   waiting    = turnaround - burst   the part of that spent NOT running
 *   response   = start - arrival      how long before it first ran
 *
 * `waiting = turnaround - burst` works because every time unit between arrival
 * and finish is either CPU time (exactly `burst` of them) or waiting.
 */
struct metrics compute_metrics(const struct segment segs[], int nsegs,
                               const struct process procs[], int n)
{
    struct metrics m;
    int    total_burst = 0;
    int    end         = nsegs > 0 ? segs[nsegs - 1].end : 0;
    int    idle        = 0;
    int    switches    = 0;
    int    last_proc   = -2;
    double sum_t = 0, sum_w = 0, sum_r = 0;

    memset(&m, 0, sizeof m);

    for (int i = 0; i < n; i++) {
        int turnaround = procs[i].finish_time - procs[i].arrival;

        total_burst += procs[i].burst;
        sum_t       += turnaround;
        sum_w       += turnaround - procs[i].burst;
        sum_r       += procs[i].start_time - procs[i].arrival;
    }

    /* A context switch is the CPU moving from one process to another. An idle
     * gap on its own does not count as a switch. */
    for (int i = 0; i < nsegs; i++) {
        if (segs[i].proc < 0) {
            idle += segs[i].end - segs[i].start;
            continue;
        }
        if (last_proc != -2 && segs[i].proc != last_proc)
            switches++;
        last_proc = segs[i].proc;
    }

    m.avg_turnaround = n > 0 ? sum_t / n : 0.0;
    m.avg_waiting    = n > 0 ? sum_w / n : 0.0;
    m.avg_response   = n > 0 ? sum_r / n : 0.0;
    m.total_time     = end;
    m.idle_time      = idle;
    m.switches       = switches;
    m.utilisation    = end > 0 ? 100.0 * total_burst / end : 0.0;
    m.throughput     = end > 0 ? (double)n / end : 0.0;
    return m;
}

/* ======================================================================== */
/* Printing                                                                 */
/* ======================================================================== */

void print_input(const struct process procs[], int n)
{
    printf("Workload (%d processes)\n", n);
    printf("  %-*s %8s %8s %9s\n", NAME_LEN, "process", "arrival", "burst",
           "priority");
    for (int i = 0; i < n; i++)
        printf("  %-*s %8d %8d %9d\n", NAME_LEN, procs[i].name,
               procs[i].arrival, procs[i].burst, procs[i].priority);
    printf("  (priority is only used by the 'priority' algorithm; "
           "a smaller number means more important)\n\n");
}

/* The label that goes inside a Gantt block. */
static const char *seg_label(const struct segment *s,
                             const struct process procs[])
{
    return s->proc < 0 ? "idle" : procs[s->proc].name;
}

void print_gantt(const struct segment segs[], int nsegs,
                 const struct process procs[], int n)
{
    (void)n;    /* the process count is not needed here, only the names */

    printf("Gantt chart\n");

    /* Every block is printed as "| label |", so its width depends on the
     * length of the label. The loops below print the four lines of the chart
     * using exactly the same widths, which is what keeps them aligned. */

    /* top border */
    printf("  ");
    for (int i = 0; i < nsegs; i++) {
        int w = (int)strlen(seg_label(&segs[i], procs)) + 2;
        printf("+");
        for (int k = 0; k < w; k++)
            printf("-");
    }
    printf("+\n");

    /* labels */
    printf("  ");
    for (int i = 0; i < nsegs; i++)
        printf("| %s ", seg_label(&segs[i], procs));
    printf("|\n");

    /* bottom border */
    printf("  ");
    for (int i = 0; i < nsegs; i++) {
        int w = (int)strlen(seg_label(&segs[i], procs)) + 2;
        printf("+");
        for (int k = 0; k < w; k++)
            printf("-");
    }
    printf("+\n");

    /* the time under each block boundary */
    printf("  ");
    for (int i = 0; i < nsegs; i++) {
        int w       = (int)strlen(seg_label(&segs[i], procs)) + 3;
        int printed = printf("%d", segs[i].start);
        for (int k = printed; k < w; k++)
            printf(" ");
    }
    printf("%d\n\n", nsegs > 0 ? segs[nsegs - 1].end : 0);
}

/* Which process (if any) held the CPU during time unit t? -1 = idle. */
static int who_ran_at(const struct segment segs[], int nsegs, int t)
{
    for (int i = 0; i < nsegs; i++)
        if (t >= segs[i].start && t < segs[i].end)
            return segs[i].proc;
    return -1;
}

void print_timeline(const struct segment segs[], int nsegs,
                    const struct process procs[], int n)
{
    int end = nsegs > 0 ? segs[nsegs - 1].end : 0;

    if (end > MAX_TIME)
        end = MAX_TIME;

    printf("Per-process timeline   ('#' running, '.' waiting for the CPU)\n");
    for (int i = 0; i < n; i++) {
        printf("  %-*s ", NAME_LEN, procs[i].name);
        for (int t = 0; t < end; t++) {
            if (t < procs[i].arrival || t >= procs[i].finish_time)
                putchar(' ');           /* not arrived yet, or already done */
            else if (who_ran_at(segs, nsegs, t) == i)
                putchar('#');           /* on the CPU */
            else
                putchar('.');           /* ready, but somebody else is running */
        }
        printf("  finished at %d\n", procs[i].finish_time);
    }
    printf("\n");
}

void print_summary(const char *algorithm, const struct segment segs[],
                   int nsegs, const struct process procs[], int n)
{
    struct metrics m = compute_metrics(segs, nsegs, procs, n);

    printf("Results (%s)\n", algorithm);
    printf("  %-*s %8s %8s %8s %8s %11s %8s %9s\n", NAME_LEN, "process",
           "arrival", "burst", "start", "finish", "turnaround", "waiting",
           "response");

    for (int i = 0; i < n; i++) {
        int turnaround = procs[i].finish_time - procs[i].arrival;

        printf("  %-*s %8d %8d %8d %8d %11d %8d %9d\n", NAME_LEN,
               procs[i].name, procs[i].arrival, procs[i].burst,
               procs[i].start_time, procs[i].finish_time, turnaround,
               turnaround - procs[i].burst,
               procs[i].start_time - procs[i].arrival);
    }

    printf("\n");
    printf("  average turnaround time : %.2f\n", m.avg_turnaround);
    printf("  average waiting time    : %.2f\n", m.avg_waiting);
    printf("  average response time   : %.2f\n", m.avg_response);
    printf("  total time              : %d (%d busy, %d idle)\n",
           m.total_time, m.total_time - m.idle_time, m.idle_time);
    printf("  CPU utilisation         : %.1f%%\n", m.utilisation);
    printf("  throughput              : %.3f processes per time unit\n",
           m.throughput);
    printf("  context switches        : %d\n", m.switches);
    printf("\n");
}

void print_csv(const char *algorithm, const struct process procs[], int n,
               int with_header)
{
    if (with_header)
        printf("algorithm,process,arrival,burst,priority,start,finish,"
               "turnaround,waiting,response\n");

    for (int i = 0; i < n; i++) {
        int turnaround = procs[i].finish_time - procs[i].arrival;

        printf("%s,%s,%d,%d,%d,%d,%d,%d,%d,%d\n", algorithm, procs[i].name,
               procs[i].arrival, procs[i].burst, procs[i].priority,
               procs[i].start_time, procs[i].finish_time, turnaround,
               turnaround - procs[i].burst,
               procs[i].start_time - procs[i].arrival);
    }
}
