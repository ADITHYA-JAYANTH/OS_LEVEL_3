/*
 * main.c - FR8: command-line interface and error handling.
 *
 * Usage: ./campuscompute <command> [arguments]   (see print_usage)
 */
#include "campuscompute.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_usage(FILE *out)
{
    fprintf(out,
        "CampusCompute - job scheduling and resource management prototype\n"
        "\n"
        "Usage: ./campuscompute <command> [arguments]\n"
        "\n"
        "Scheduling (job file format: 'id arrival burst priority' per line):\n"
        "  fcfs     [jobfile]               First Come First Served\n"
        "  rr       [jobfile] [quantum]     Round Robin (default quantum %d)\n"
        "  priority [jobfile]               Non-preemptive priority (lower = higher)\n"
        "  compare  [jobfile] [quantum]     All three policies on the same job set\n"
        "                                   (default jobfile: %s)\n"
        "Resource safety:\n"
        "  banker   <safe|unsafe|statefile> Banker's safety algorithm\n"
        "Concurrency / IPC / processes:\n"
        "  logger                           Producer-consumer logger (pthreads)\n"
        "  ipc                              Shared memory + named semaphore\n"
        "  worker   <normal|fail>           fork()/waitpid(); 'fail' = simulated failure\n"
        "  rlimit                           setrlimit() demonstration\n"
        "  cleanup                          Remove resources created by this program\n"
        "Other:\n"
        "  partA                            Run all Part A demos\n"
        "  all                              Integrated demonstration\n"
        "  help                             Show this message\n",
        DEFAULT_QUANTUM, DEFAULT_JOBFILE);
}

static int usage_error(const char *msg)
{
    fprintf(stderr, "campuscompute: %s\n", msg);
    fprintf(stderr, "Try './campuscompute help' for the list of commands.\n");
    return 2;
}

/* Parses a quantum argument; returns -1 and prints an error if malformed. */
static int parse_quantum(const char *s)
{
    char *end;
    errno = 0;
    long v = strtol(s, &end, 10);
    if (errno || end == s || *end != '\0' || v < 1 || v > MAX_QUANTUM) {
        fprintf(stderr, "campuscompute: invalid time quantum '%s' (need integer 1-%d)\n", s, MAX_QUANTUM);
        return -1;
    }
    return (int)v;
}

int run_part_a_demo(const char *jobfile, int quantum)
{
    int rc = 0;
    rc |= run_compare(jobfile, quantum) != 0;
    rc |= run_banker("safe") != 0;
    rc |= run_banker("unsafe") != 0;
    return rc;
}

int main(int argc, char *argv[])
{
    if (argc < 2) {
        print_usage(stderr);
        return 1;
    }
    const char *cmd = argv[1];
    int rc;

    if (!strcmp(cmd, "help") || !strcmp(cmd, "-h") || !strcmp(cmd, "--help")) {
        print_usage(stdout);
        return 0;
    }

    if (!strcmp(cmd, "fcfs") || !strcmp(cmd, "priority")) {
        if (argc > 3) return usage_error("usage: ./campuscompute fcfs|priority [jobfile]");
        const char *file = argc == 3 ? argv[2] : DEFAULT_JOBFILE;
        rc = run_scheduler(!strcmp(cmd, "fcfs") ? ALGO_FCFS : ALGO_PRIORITY, file, 0);
    } else if (!strcmp(cmd, "rr") || !strcmp(cmd, "compare")) {
        if (argc > 4) return usage_error("usage: ./campuscompute rr|compare [jobfile] [quantum]");
        const char *file = argc >= 3 ? argv[2] : DEFAULT_JOBFILE;
        int q = DEFAULT_QUANTUM;
        if (argc == 4 && (q = parse_quantum(argv[3])) < 0) return 2;
        rc = !strcmp(cmd, "rr") ? run_scheduler(ALGO_RR, file, q) : run_compare(file, q);
    } else if (!strcmp(cmd, "banker")) {
        if (argc != 3) return usage_error("usage: ./campuscompute banker <safe|unsafe|statefile>");
        rc = run_banker(argv[2]);
    } else if (!strcmp(cmd, "logger")) {
        if (argc != 2) return usage_error("usage: ./campuscompute logger");
        rc = run_logger_demo();
    } else if (!strcmp(cmd, "ipc")) {
        if (argc != 2) return usage_error("usage: ./campuscompute ipc");
        rc = run_ipc_demo();
    } else if (!strcmp(cmd, "worker")) {
        if (argc != 3 || (strcmp(argv[2], "normal") && strcmp(argv[2], "fail")))
            return usage_error("usage: ./campuscompute worker <normal|fail>");
        rc = run_worker_demo(!strcmp(argv[2], "fail"));
    } else if (!strcmp(cmd, "rlimit")) {
        if (argc != 2) return usage_error("usage: ./campuscompute rlimit");
        rc = run_rlimit_demo();
    } else if (!strcmp(cmd, "cleanup")) {
        if (argc != 2) return usage_error("usage: ./campuscompute cleanup");
        rc = run_cleanup();
    } else if (!strcmp(cmd, "partA")) {
        rc = run_part_a_demo(DEFAULT_JOBFILE, DEFAULT_QUANTUM);
    } else if (!strcmp(cmd, "all")) {
        rc = run_all(DEFAULT_JOBFILE, DEFAULT_QUANTUM);
    } else {
        char msg[160];
        snprintf(msg, sizeof msg, "unknown command '%.60s'", cmd);
        return usage_error(msg);
    }
    return rc == 0 ? 0 : 1;
}
