/*
 * scheduler.c - FR1 (job management) and FR2 (CPU scheduling).
 *
 * Policies: FCFS, Round Robin, non-preemptive Priority.
 *
 * Tie-breaking rules (all policies):
 *   FCFS      : earlier arrival first; equal arrival -> lower job ID.
 *   Priority  : lower priority number (= higher priority) first;
 *               then earlier arrival; then lower job ID.
 *   RR        : ready queue is FIFO. Jobs that arrive while a slice is
 *               running are queued BEFORE the preempted job is re-queued.
 *               Simultaneous arrivals are queued in job-ID order.
 * Idle gaps (no job has arrived) are shown as IDLE in the timeline.
 */
#include "campuscompute.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { int job_id; int start; int end; } segment_t; /* job_id -1 = idle */
typedef struct { segment_t *seg; int count; } timeline_t;

const char *job_status_name(job_status_t s)
{
    switch (s) {
    case JOB_NEW:     return "NEW";
    case JOB_READY:   return "READY";
    case JOB_RUNNING: return "RUNNING";
    case JOB_DONE:    return "DONE";
    }
    return "?";
}

/* ------------------------------------------------------------------ */
/* Input parsing and validation                                        */
/* ------------------------------------------------------------------ */

static int parse_field(const char *path, int lineno, const char *name,
                       const char *tok, long lo, long hi, int *out)
{
    char *end;
    errno = 0;
    long v = strtol(tok, &end, 10);
    if (errno != 0 || end == tok || *end != '\0' || v < lo || v > hi) {
        fprintf(stderr, "error: %s:%d: invalid %s '%s' (need an integer in [%ld, %ld])\n",
                path, lineno, name, tok, lo, hi);
        return -1;
    }
    *out = (int)v;
    return 0;
}

int load_jobs(const char *path, job_t jobs[], int *n_out)
{
    FILE *fp = fopen(path, "r");
    if (!fp) {
        fprintf(stderr, "error: cannot open job file '%s': %s\n", path, strerror(errno));
        return -1;
    }

    char line[256];
    int lineno = 0, n = 0, rc = 0;

    while (fgets(line, sizeof line, fp)) {
        lineno++;
        if (!strchr(line, '\n') && !feof(fp)) {
            fprintf(stderr, "error: %s:%d: line too long\n", path, lineno);
            rc = -1;
            break;
        }
        char *hash = strchr(line, '#');           /* strip comments */
        if (hash) *hash = '\0';

        char *tok[5], *save = NULL;
        int nt = 0;
        for (char *t = strtok_r(line, " \t\r\n", &save); t; t = strtok_r(NULL, " \t\r\n", &save)) {
            if (nt < 5) tok[nt] = t;
            nt++;
        }
        if (nt == 0) continue;                    /* blank line */
        if (nt != 4) {
            fprintf(stderr, "error: %s:%d: expected 4 fields (id arrival burst priority), found %d\n",
                    path, lineno, nt);
            rc = -1;
            break;
        }
        if (n >= MAX_JOBS) {
            fprintf(stderr, "error: %s:%d: too many jobs (max %d)\n", path, lineno, MAX_JOBS);
            rc = -1;
            break;
        }

        job_t j;
        memset(&j, 0, sizeof j);
        if (parse_field(path, lineno, "job id",   tok[0], 1, MAX_JOB_ID,   &j.id)       ||
            parse_field(path, lineno, "arrival",  tok[1], 0, MAX_ARRIVAL,  &j.arrival)  ||
            parse_field(path, lineno, "burst",    tok[2], 1, MAX_BURST,    &j.burst)    ||
            parse_field(path, lineno, "priority", tok[3], 0, MAX_PRIORITY, &j.priority)) {
            rc = -1;
            break;
        }
        for (int i = 0; i < n; i++) {
            if (jobs[i].id == j.id) {
                fprintf(stderr, "error: %s:%d: duplicate job id %d\n", path, lineno, j.id);
                rc = -1;
                break;
            }
        }
        if (rc) break;

        j.remaining = j.burst;
        j.start = -1;
        j.status = JOB_NEW;
        jobs[n++] = j;
    }
    fclose(fp);

    if (rc == 0 && n == 0) {
        fprintf(stderr, "error: %s: no jobs found\n", path);
        rc = -1;
    }
    if (rc == 0) *n_out = n;
    return rc;
}

/* ------------------------------------------------------------------ */
/* Timeline helpers                                                    */
/* ------------------------------------------------------------------ */

static void tl_add(timeline_t *t, int job_id, int start, int end)
{
    if (start >= end) return;
    if (t->count > 0) {                           /* merge back-to-back slices */
        segment_t *last = &t->seg[t->count - 1];
        if (last->job_id == job_id && last->end == start) {
            last->end = end;
            return;
        }
    }
    t->seg[t->count].job_id = job_id;
    t->seg[t->count].start = start;
    t->seg[t->count].end = end;
    t->count++;
}

static int arrives_before(const job_t *a, const job_t *b)
{
    if (a->arrival != b->arrival) return a->arrival < b->arrival;
    return a->id < b->id;
}

static void order_by_arrival(const job_t *jobs, int n, int idx[])
{
    for (int i = 0; i < n; i++) {                 /* insertion sort, n <= 64 */
        int k = i;
        while (k > 0 && arrives_before(&jobs[i], &jobs[idx[k - 1]])) {
            idx[k] = idx[k - 1];
            k--;
        }
        idx[k] = i;
    }
}

/* ------------------------------------------------------------------ */
/* The three scheduling policies                                       */
/* ------------------------------------------------------------------ */

static void sim_fcfs(job_t *jobs, int n, timeline_t *tl)
{
    int idx[MAX_JOBS], t = 0;
    order_by_arrival(jobs, n, idx);
    for (int k = 0; k < n; k++) {
        job_t *j = &jobs[idx[k]];
        if (t < j->arrival) {
            tl_add(tl, -1, t, j->arrival);
            t = j->arrival;
        }
        j->status = JOB_RUNNING;
        j->start = t;
        tl_add(tl, j->id, t, t + j->burst);
        t += j->burst;
        j->remaining = 0;
        j->completion = t;
        j->status = JOB_DONE;
    }
}

static int better_priority(const job_t *a, const job_t *b)
{
    if (a->priority != b->priority) return a->priority < b->priority;
    return arrives_before(a, b);
}

static void sim_priority(job_t *jobs, int n, timeline_t *tl)
{
    int done = 0, t = 0;
    while (done < n) {
        int best = -1;
        for (int i = 0; i < n; i++) {
            if (jobs[i].status == JOB_DONE || jobs[i].arrival > t) continue;
            if (best < 0 || better_priority(&jobs[i], &jobs[best])) best = i;
        }
        if (best < 0) {                           /* CPU idle until next arrival */
            int next = INT_MAX;
            for (int i = 0; i < n; i++)
                if (jobs[i].status != JOB_DONE && jobs[i].arrival < next) next = jobs[i].arrival;
            tl_add(tl, -1, t, next);
            t = next;
            continue;
        }
        job_t *j = &jobs[best];
        j->status = JOB_RUNNING;
        j->start = t;
        tl_add(tl, j->id, t, t + j->burst);
        t += j->burst;
        j->remaining = 0;
        j->completion = t;
        j->status = JOB_DONE;
        done++;
    }
}

typedef struct { int buf[MAX_JOBS + 1]; int head, count; } queue_t;

static void q_push(queue_t *q, int v) { q->buf[(q->head + q->count++) % (MAX_JOBS + 1)] = v; }
static int  q_pop(queue_t *q)
{
    int v = q->buf[q->head];
    q->head = (q->head + 1) % (MAX_JOBS + 1);
    q->count--;
    return v;
}

static void sim_rr(job_t *jobs, int n, int quantum, timeline_t *tl)
{
    int idx[MAX_JOBS], next = 0, t = 0, done = 0;
    queue_t q = { .head = 0, .count = 0 };
    order_by_arrival(jobs, n, idx);

#define ADMIT_ARRIVALS()                                              \
    while (next < n && jobs[idx[next]].arrival <= t) {                \
        jobs[idx[next]].status = JOB_READY;                           \
        q_push(&q, idx[next]);                                        \
        next++;                                                       \
    }

    while (done < n) {
        ADMIT_ARRIVALS();
        if (q.count == 0) {                       /* idle: jump to next arrival */
            int a = jobs[idx[next]].arrival;
            tl_add(tl, -1, t, a);
            t = a;
            continue;
        }
        int i = q_pop(&q);
        job_t *j = &jobs[i];
        if (j->start < 0) j->start = t;
        j->status = JOB_RUNNING;

        int run = j->remaining < quantum ? j->remaining : quantum;
        tl_add(tl, j->id, t, t + run);
        t += run;
        j->remaining -= run;

        ADMIT_ARRIVALS();                         /* arrivals precede the preempted job */
        if (j->remaining > 0) {
            j->status = JOB_READY;
            q_push(&q, i);
        } else {
            j->completion = t;
            j->status = JOB_DONE;
            done++;
        }
    }
#undef ADMIT_ARRIVALS
}

/* ------------------------------------------------------------------ */
/* Metrics and reporting                                               */
/* ------------------------------------------------------------------ */

static void compute_metrics(job_t *jobs, int n, const char *name, sched_result_t *res)
{
    double sum_tat = 0, sum_wt = 0;
    int makespan = 0, busy = 0;
    for (int i = 0; i < n; i++) {
        jobs[i].turnaround = jobs[i].completion - jobs[i].arrival;
        jobs[i].waiting = jobs[i].turnaround - jobs[i].burst;
        sum_tat += jobs[i].turnaround;
        sum_wt += jobs[i].waiting;
        busy += jobs[i].burst;
        if (jobs[i].completion > makespan) makespan = jobs[i].completion;
    }
    res->name = name;
    res->avg_turnaround = sum_tat / n;
    res->avg_waiting = sum_wt / n;
    res->makespan = makespan;
    res->utilization = makespan > 0 ? 100.0 * busy / makespan : 0.0;
}

static void label_of(const segment_t *s, char *buf, size_t len)
{
    if (s->job_id < 0) snprintf(buf, len, "IDLE");
    else               snprintf(buf, len, "J%d", s->job_id);
}

static void print_report(const char *name, const char *rules, const job_t *jobs, int n,
                         const timeline_t *tl, const sched_result_t *res)
{
    char lab[16];

    printf("\n=== %s ===\n", name);
    printf("Rules: %s\n\n", rules);

    printf("Execution order:");
    for (int i = 0; i < tl->count; i++) {
        label_of(&tl->seg[i], lab, sizeof lab);
        if (i % 12 == 0 && i > 0) printf("\n                ");
        printf("%s%s", i ? " -> " : " ", lab);
    }
    printf("\n\nTimeline (start-end):\n");
    for (int i = 0; i < tl->count; i++) {
        label_of(&tl->seg[i], lab, sizeof lab);
        printf("  [%4d - %4d]  %s\n", tl->seg[i].start, tl->seg[i].end, lab);
    }

    printf("\nGantt chart:\n");
    for (int base = 0; base < tl->count; base += 10) {
        int end = base + 10 < tl->count ? base + 10 : tl->count;
        for (int i = base; i < end; i++) {
            label_of(&tl->seg[i], lab, sizeof lab);
            printf("|%-6s", lab);
        }
        printf("|\n");
        for (int i = base; i < end; i++) printf("%-7d", tl->seg[i].start);
        printf("%d\n", tl->seg[end - 1].end);
    }

    printf("\n%-5s %7s %6s %5s %6s %7s %5s %5s  %s\n",
           "Job", "Arrive", "Burst", "Prio", "Start", "Finish", "TAT", "WT", "Status");
    for (int i = 0; i < n; i++) {
        const job_t *j = &jobs[i];
        printf("J%-4d %7d %6d %5d %6d %7d %5d %5d  %s\n", j->id, j->arrival, j->burst,
               j->priority, j->start, j->completion, j->turnaround, j->waiting,
               job_status_name(j->status));
    }
    printf("\nAverage turnaround time : %.2f\n", res->avg_turnaround);
    printf("Average waiting time    : %.2f\n", res->avg_waiting);
    printf("Makespan                : %d   CPU utilisation: %.1f%%\n", res->makespan, res->utilization);
}

static int simulate(algo_t algo, const job_t *input, int n, int quantum,
                    sched_result_t *res, int print)
{
    job_t jobs[MAX_JOBS];
    memcpy(jobs, input, (size_t)n * sizeof(job_t));

    long total = 0;
    for (int i = 0; i < n; i++) {
        jobs[i].remaining = jobs[i].burst;
        jobs[i].start = -1;
        jobs[i].completion = 0;
        jobs[i].status = JOB_NEW;
        total += jobs[i].burst;
    }

    timeline_t tl = { .seg = malloc(((size_t)total + (size_t)n + 2) * sizeof(segment_t)), .count = 0 };
    if (!tl.seg) {
        fprintf(stderr, "error: out of memory\n");
        return -1;
    }

    const char *name, *rules;
    switch (algo) {
    case ALGO_FCFS:
        name = "FCFS";
        rules = "non-preemptive; ties on arrival broken by lower job ID.";
        sim_fcfs(jobs, n, &tl);
        break;
    case ALGO_PRIORITY:
        name = "Priority (non-preemptive)";
        rules = "lower number = higher priority; ties -> earlier arrival -> lower job ID.";
        sim_priority(jobs, n, &tl);
        break;
    default: {
        static char rr_name[48], rr_rules[160];
        snprintf(rr_name, sizeof rr_name, "Round Robin (quantum = %d)", quantum);
        snprintf(rr_rules, sizeof rr_rules,
                 "time quantum = %d; FIFO ready queue; new arrivals queued before the preempted job.",
                 quantum);
        name = rr_name;
        rules = rr_rules;
        sim_rr(jobs, n, quantum, &tl);
        break;
    }
    }

    compute_metrics(jobs, n, name, res);
    if (print) print_report(name, rules, jobs, n, &tl, res);
    free(tl.seg);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Public entry points                                                 */
/* ------------------------------------------------------------------ */

static int check_quantum(int quantum)
{
    if (quantum < 1 || quantum > MAX_QUANTUM) {
        fprintf(stderr, "error: time quantum %d out of range [1, %d]\n", quantum, MAX_QUANTUM);
        return -1;
    }
    return 0;
}

int run_scheduler(algo_t algo, const char *jobfile, int quantum)
{
    job_t jobs[MAX_JOBS];
    int n;
    sched_result_t res;

    if (algo == ALGO_RR && check_quantum(quantum) != 0) return -1;
    if (load_jobs(jobfile, jobs, &n) != 0) return -1;
    printf("Loaded %d job(s) from %s\n", n, jobfile);
    return simulate(algo, jobs, n, quantum, &res, 1);
}

int run_compare(const char *jobfile, int quantum)
{
    job_t jobs[MAX_JOBS];
    int n;
    sched_result_t r[3];
    static const algo_t algos[3] = { ALGO_FCFS, ALGO_RR, ALGO_PRIORITY };

    if (check_quantum(quantum) != 0) return -1;
    if (load_jobs(jobfile, jobs, &n) != 0) return -1;

    printf("\n=== Scheduling comparison (same %d-job set from %s) ===\n\n", n, jobfile);
    printf("%-5s %7s %6s %5s\n", "Job", "Arrive", "Burst", "Prio");
    for (int i = 0; i < n; i++)
        printf("J%-4d %7d %6d %5d\n", jobs[i].id, jobs[i].arrival, jobs[i].burst, jobs[i].priority);

    for (int a = 0; a < 3; a++)
        if (simulate(algos[a], jobs, n, quantum, &r[a], 0) != 0) return -1;

    printf("\n%-28s %10s %10s %9s %7s\n", "Policy", "Avg WT", "Avg TAT", "Makespan", "CPU %");
    int best_wt = 0, best_tat = 0;
    for (int a = 0; a < 3; a++) {
        printf("%-28s %10.2f %10.2f %9d %6.1f%%\n", r[a].name, r[a].avg_waiting,
               r[a].avg_turnaround, r[a].makespan, r[a].utilization);
        if (r[a].avg_waiting < r[best_wt].avg_waiting) best_wt = a;
        if (r[a].avg_turnaround < r[best_tat].avg_turnaround) best_tat = a;
    }
    printf("\nLowest average waiting time   : %s (%.2f)\n", r[best_wt].name, r[best_wt].avg_waiting);
    printf("Lowest average turnaround time: %s (%.2f)\n", r[best_tat].name, r[best_tat].avg_turnaround);
    return 0;
}
