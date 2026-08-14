/*
 * scheduler_common.c - workload loading and result printing
 * ---------------------------------------------------------
 * Shared by fcfs.c and mlfq.c so that both print their results in exactly the
 * same shape and can be compared line by line.
 */

#include "scheduler.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

/* ======================================================================== */
/* Reading the workload                                                     */
/* ======================================================================== */

/* Set the fields the simulation owns to their starting values. */
static void init_process(struct process *p)
{
    p->remaining   = p->burst;
    p->queue       = 0;         /* everyone starts in the highest MLFQ queue */
    p->start_time  = -1;        /* -1 means "has not run yet"                */
    p->finish_time = -1;
}

int load_workload(const char *path, struct process procs[], int max)
{
    FILE *fp;
    char  line[256];
    int   n       = 0;
    int   lineno  = 0;

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
        int  i = 0;

        lineno++;

        while (isspace((unsigned char)line[i]))
            i++;
        if (line[i] == '\0' || line[i] == '#')
            continue;                   /* blank line or comment */

        /* %11s stops one character before the end of name[] so a very long
         * name cannot run off the end of the buffer. */
        if (sscanf(line + i, "%11s %d %d", name, &arrival, &burst) != 3) {
            fprintf(stderr, "error: %s line %d: expected "
                            "\"name arrival burst\"\n", path, lineno);
            goto fail;
        }
        if (arrival < 0 || burst <= 0) {
            fprintf(stderr, "error: %s line %d: arrival must be >= 0 and "
                            "burst must be > 0 (got %d and %d)\n",
                    path, lineno, arrival, burst);
            goto fail;
        }
        if (n == max) {
            fprintf(stderr, "error: too many processes (limit is %d)\n", max);
            goto fail;
        }

        snprintf(procs[n].name, NAME_LEN, "%s", name);
        procs[n].arrival = arrival;
        procs[n].burst   = burst;
        init_process(&procs[n]);
        n++;
    }

    if (fp != stdin)
        fclose(fp);

    if (n == 0) {
        fprintf(stderr, "error: %s contains no processes\n", path);
        return -1;
    }
    return n;

fail:
    if (fp != stdin)
        fclose(fp);
    return -1;
}

int load_demo_workload(struct process procs[])
{
    /* Chosen so that the two algorithms behave differently: P1 is a long job
     * and P4 is a short one that arrives late, which FCFS makes wait for a
     * long time and MLFQ lets finish quickly. */
    static const struct { const char *name; int arrival, burst; } demo[] = {
        {"P1", 0, 8},
        {"P2", 1, 4},
        {"P3", 2, 9},
        {"P4", 3, 2},
    };
    int n = (int)(sizeof demo / sizeof demo[0]);

    for (int i = 0; i < n; i++) {
        snprintf(procs[i].name, NAME_LEN, "%s", demo[i].name);
        procs[i].arrival = demo[i].arrival;
        procs[i].burst   = demo[i].burst;
        init_process(&procs[i]);
    }
    return n;
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
/* Printing                                                                 */
/* ======================================================================== */

void print_input(const struct process procs[], int n)
{
    printf("Workload (%d processes)\n", n);
    printf("  %-*s %8s %8s\n", NAME_LEN, "process", "arrival", "burst");
    for (int i = 0; i < n; i++)
        printf("  %-*s %8d %8d\n", NAME_LEN, procs[i].name,
               procs[i].arrival, procs[i].burst);
    printf("\n");
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
     * length of the label. The three loops below print the three lines of the
     * chart using exactly the same widths, which is what keeps them aligned. */

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
    int total_burst = 0;
    int end         = nsegs > 0 ? segs[nsegs - 1].end : 0;
    int first       = nsegs > 0 ? segs[0].start : 0;
    int switches    = 0;
    int last_proc   = -2;

    double sum_turnaround = 0, sum_waiting = 0, sum_response = 0;

    printf("Results (%s)\n", algorithm);
    printf("  %-*s %8s %8s %8s %8s %11s %8s %9s\n", NAME_LEN, "process",
           "arrival", "burst", "start", "finish", "turnaround", "waiting",
           "response");

    for (int i = 0; i < n; i++) {
        /* turnaround = total time from arriving to finishing
         * waiting    = turnaround minus the CPU time it actually used
         * response   = how long it waited before it first ran            */
        int turnaround = procs[i].finish_time - procs[i].arrival;
        int waiting    = turnaround - procs[i].burst;
        int response   = procs[i].start_time - procs[i].arrival;

        printf("  %-*s %8d %8d %8d %8d %11d %8d %9d\n", NAME_LEN,
               procs[i].name, procs[i].arrival, procs[i].burst,
               procs[i].start_time, procs[i].finish_time, turnaround,
               waiting, response);

        total_burst    += procs[i].burst;
        sum_turnaround += turnaround;
        sum_waiting    += waiting;
        sum_response   += response;
    }

    /* A context switch is the CPU moving from one process to another. Idle
     * gaps do not count as a switch by themselves. */
    for (int i = 0; i < nsegs; i++) {
        if (segs[i].proc < 0)
            continue;
        if (last_proc != -2 && segs[i].proc != last_proc)
            switches++;
        last_proc = segs[i].proc;
    }

    printf("\n");
    printf("  average turnaround time : %.2f\n", sum_turnaround / n);
    printf("  average waiting time    : %.2f\n", sum_waiting / n);
    printf("  average response time   : %.2f\n", sum_response / n);
    printf("  total time              : %d (from %d to %d)\n",
           end - first, first, end);
    printf("  CPU utilisation         : %.1f%% (%d busy of %d)\n",
           end > 0 ? 100.0 * total_burst / end : 0.0, total_burst, end);
    printf("  throughput              : %.3f processes per time unit\n",
           end > 0 ? (double)n / end : 0.0);
    printf("  context switches        : %d\n", switches);
    printf("\n");
}
