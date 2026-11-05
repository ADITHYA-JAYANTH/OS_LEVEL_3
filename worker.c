/*
 * worker.c - FR6: worker failure detection with fork() and waitpid().
 *
 * The parent creates three worker processes and collects each one's status
 * with waitpid(). In 'normal' mode every worker finishes with exit code 0.
 * In 'fail' mode two workers FAIL ON PURPOSE (these are simulations written
 * into this program, not real OS faults):
 *   worker 2 : exits with the non-zero status 42
 *   worker 3 : kills itself with SIGKILL (abnormal termination by signal)
 * The parent decodes each status with WIFEXITED / WEXITSTATUS /
 * WIFSIGNALED / WTERMSIG and compares it with the planned outcome.
 */
#include "partb.h"

#include <signal.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define N_WORKERS     3
#define SIM_EXIT_CODE 42

/* WCOREDUMP is not part of strict POSIX; fall back to "unknown" if absent. */
#ifdef WCOREDUMP
#define CORE_DUMPED(st) WCOREDUMP(st)
#else
#define CORE_DUMPED(st) 0
#endif

status_kind_t describe_status(int status, int *value, char *buf, size_t len)
{
    if (WIFEXITED(status)) {
        int code = WEXITSTATUS(status);
        *value = code;
        if (code == 0)
            snprintf(buf, len, "exited normally (status 0)");
        else
            snprintf(buf, len, "exited with non-zero status %d", code);
        return code == 0 ? ST_EXIT_OK : ST_EXIT_FAIL;
    }
    if (WIFSIGNALED(status)) {
        int sig = WTERMSIG(status);
        *value = sig;
        snprintf(buf, len, "was terminated by signal %d (%s)%s", sig, strsignal(sig),
                 CORE_DUMPED(status) ? ", core dumped" : "");
        return ST_SIGNALED;
    }
    *value = status;
    snprintf(buf, len, "ended in an unexpected state (raw status 0x%x)", status);
    return ST_OTHER;
}

/* What the parent should observe for worker idx (1-based) in each mode. */
static void expected_outcome(int idx, int fail_mode, status_kind_t *kind, int *value)
{
    *kind = ST_EXIT_OK;
    *value = 0;
    if (fail_mode && idx == 2) { *kind = ST_EXIT_FAIL; *value = SIM_EXIT_CODE; }
    if (fail_mode && idx == 3) { *kind = ST_SIGNALED;  *value = SIGKILL; }
}

/* Runs in the child process. Never returns. */
static void worker_body(int idx, int fail_mode)
{
    unsigned long sum = 0;

    msleep(30);                                 /* let the parent print its "created" line first */
    printf("  [worker %d, pid %d] started: running a simulated CPU job\n", idx, (int)getpid());
    fflush(stdout);
    for (unsigned long i = 1; i <= 2000000UL * (unsigned long)idx; i++)
        sum += i % 7;
    msleep(100L * idx);                         /* staggered so finish order is repeatable */

    if (fail_mode && idx == 2) {
        printf("  [worker 2] SIMULATED FAILURE (deliberate, not a real OS fault): exit(%d)\n", SIM_EXIT_CODE);
        fflush(stdout);
        _exit(SIM_EXIT_CODE);
    }
    if (fail_mode && idx == 3) {
        printf("  [worker 3] SIMULATED CRASH (deliberate, not a real OS fault): raising SIGKILL on itself\n");
        fflush(stdout);
        raise(SIGKILL);
        _exit(99);                              /* not reached */
    }
    printf("  [worker %d] job finished normally (checksum %lu)\n", idx, sum);
    fflush(stdout);
    _exit(0);
}

int run_worker_demo(int simulate_failure)
{
    pid_t pids[N_WORKERS + 1] = { 0 };
    int started = 0, remaining, ok = 0, failed = 0, mismatches = 0;

    print_banner(simulate_failure ? "Worker processes (with SIMULATED failures)"
                                  : "Worker processes (normal run)");
    if (simulate_failure)
        printf("NOTE: the failures below are simulated on purpose by this program.\n"
               "      They are not real operating-system failures.\n");

    for (int idx = 1; idx <= N_WORKERS; idx++) {
        fflush(stdout);                         /* empty the buffer before fork */
        pid_t pid = fork();
        if (pid < 0) {
            fprintf(stderr, "worker: fork failed for worker %d: %s\n", idx, strerror(errno));
            mismatches++;
            break;
        }
        if (pid == 0)
            worker_body(idx, simulate_failure); /* never returns */
        pids[idx] = pid;
        started++;
        printf("parent: created worker %d (pid %d)\n", idx, (int)pid);
        fflush(stdout);
    }

    remaining = started;
    while (remaining > 0) {
        int status, idx = 0, value;
        char desc[128];
        status_kind_t kind, want_kind;
        int want_value;
        pid_t w = waitpid(-1, &status, 0);

        if (w < 0) {
            if (errno == EINTR) continue;
            fprintf(stderr, "worker: waitpid: %s\n", strerror(errno));
            mismatches++;
            break;
        }
        for (int i = 1; i <= N_WORKERS; i++)
            if (pids[i] == w) idx = i;
        if (idx == 0)                           /* not one of ours */
            continue;

        kind = describe_status(status, &value, desc, sizeof desc);
        expected_outcome(idx, simulate_failure, &want_kind, &want_value);
        if (kind == ST_EXIT_OK) ok++; else failed++;

        printf("parent: worker %d (pid %d) %s\n", idx, (int)w, desc);
        if (kind == want_kind && value == want_value) {
            printf("        -> %s\n", kind == ST_EXIT_OK ? "result: SUCCESS"
                                                         : "result: FAILURE DETECTED (simulated, as planned)");
        } else {
            printf("        -> UNEXPECTED outcome\n");
            mismatches++;
        }
        remaining--;
    }

    /* Every child was collected, so there must be no zombies left. */
    if (waitpid(-1, NULL, WNOHANG) == -1 && errno == ECHILD)
        printf("parent: all children collected - no zombie processes remain\n");
    else {
        printf("parent: WARNING - unexpected child process still present\n");
        mismatches++;
    }

    printf("\nSummary: %d worker(s) succeeded, %d failed (%s)\n", ok, failed,
           simulate_failure ? "failures were simulated" : "no failures planned");
    if (started == N_WORKERS && mismatches == 0) {
        printf("RESULT: PASS - parent reported every worker outcome accurately\n");
        return 0;
    }
    printf("RESULT: FAIL - outcome did not match the plan\n");
    return 1;
}
