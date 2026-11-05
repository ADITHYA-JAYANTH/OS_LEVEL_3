/*
 * integrate.c - S21: integrated demonstration. Runs every feature in order
 * and prints a PASS/FAIL summary table.
 */
#include "partb.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    const char *name;
    int rc;
} stage_t;

static int count_log_lines(void)
{
    FILE *f = fopen(LOG_FILE, "r");
    char line[256];
    int n = 0;
    if (!f) return -1;
    while (fgets(line, sizeof line, f))
        if (line[0] != '#') n++;
    fclose(f);
    return n;
}

int run_all(const char *jobfile, int quantum)
{
    stage_t st[7];
    int n = 0, failures = 0;

    print_banner("CampusCompute integrated demonstration");

    st[n].name = "Scheduling + Banker's algorithm (Part A)";
    st[n++].rc = run_part_a_demo(jobfile, quantum);
    fflush(stdout);

    st[n].name = "Producer-consumer logger (threads)";
    st[n++].rc = run_logger_demo();

    st[n].name = "Shared memory + semaphores (IPC)";
    st[n++].rc = run_ipc_demo();

    st[n].name = "Worker processes - normal";
    st[n++].rc = run_worker_demo(0);

    st[n].name = "Worker processes - simulated failure";
    st[n++].rc = run_worker_demo(1);

    st[n].name = "Resource limits (setrlimit)";
    st[n++].rc = run_rlimit_demo();

    st[n].name = "Cleanup verification";
    st[n++].rc = run_cleanup();

    print_banner("Integrated summary");
    for (int i = 0; i < n; i++) {
        printf("  %-45s %s\n", st[i].name, st[i].rc == 0 ? "PASS" : "FAIL");
        if (st[i].rc != 0) failures++;
    }
    printf("\nLog file: %s (%d entries)\n", LOG_FILE, count_log_lines());
    printf("OVERALL: %s (%d of %d stages passed)\n",
           failures == 0 ? "PASS" : "FAIL", n - failures, n);
    return failures == 0 ? 0 : 1;
}
