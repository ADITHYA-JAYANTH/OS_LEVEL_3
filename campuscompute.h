/*
 * campuscompute.h - shared interfaces for CampusCompute (Part A + Part B).
 *
 * Part A owns: job model, schedulers (FCFS / RR / Priority), Banker's
 *              safety algorithm, command-line dispatcher (main.c).
 * Part B owns: logger, shared-memory IPC, worker processes, rlimits, cleanup.
 *
 * Both parts code against the prototypes below.
 */
#ifndef CAMPUSCOMPUTE_H
#define CAMPUSCOMPUTE_H

/* ---------- Limits used for input validation ---------- */
#define MAX_JOBS         64
#define MAX_JOB_ID       9999
#define MAX_ARRIVAL      100000
#define MAX_BURST        10000
#define MAX_PRIORITY     100
#define MAX_QUANTUM      1000
#define DEFAULT_QUANTUM  4
#define DEFAULT_JOBFILE  "samples/jobs.txt"

/* ---------- FR1: job model ---------- */
typedef enum { JOB_NEW, JOB_READY, JOB_RUNNING, JOB_DONE } job_status_t;

typedef struct {
    /* input fields */
    int id;
    int arrival;
    int burst;
    int priority;          /* lower number = higher priority */
    /* fields filled in by the schedulers */
    int remaining;
    int start;             /* first time the job got the CPU (-1 = never) */
    int completion;
    int turnaround;        /* completion - arrival */
    int waiting;           /* turnaround - burst   */
    job_status_t status;
} job_t;

typedef enum { ALGO_FCFS, ALGO_RR, ALGO_PRIORITY } algo_t;

typedef struct {
    const char *name;
    double avg_turnaround;
    double avg_waiting;
    double utilization;    /* percent of [0, makespan] the CPU was busy */
    int makespan;
} sched_result_t;

/* ---------- Part A: scheduler.c ---------- */
const char *job_status_name(job_status_t s);
/* Reads and validates a job file. Returns 0 on success, -1 on error
 * (a message is printed to stderr). */
int load_jobs(const char *path, job_t jobs[], int *n_out);
int run_scheduler(algo_t algo, const char *jobfile, int quantum);
int run_compare(const char *jobfile, int quantum);

/* ---------- Part A: banker.c ---------- */
/* arg is "safe", "unsafe" (built-in test cases) or a path to a state file. */
int run_banker(const char *arg);

/* ---------- Part A: main.c ---------- */
/* Runs the scheduler comparison plus both Banker's cases. */
int run_part_a_demo(const char *jobfile, int quantum);

/* ---------- Part B (implemented by the other team member) ---------- */
int run_logger_demo(void);                 /* FR3 */
int run_ipc_demo(void);                    /* FR4 */
int run_worker_demo(int simulate_failure); /* FR6 */
int run_rlimit_demo(void);                 /* FR7 */
int run_cleanup(void);                     /* FR7 / S20 */
int run_all(const char *jobfile, int quantum); /* S21: integrated demo */

#endif /* CAMPUSCOMPUTE_H */
