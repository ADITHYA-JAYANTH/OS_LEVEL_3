/*
 * limits.c - FR7: process resource limits with setrlimit().
 *
 * Each demo runs in its own forked child, so the limits never affect the
 * parent program. The parent decodes how the child ended with waitpid().
 *
 *  Demo 1  RLIMIT_AS  (address-space limit)
 *          Before the limit a 256 MB malloc() succeeds; after the child lowers
 *          its limit to 128 MB the same malloc() fails with ENOMEM while a
 *          16 MB request still works.
 *  Demo 2  RLIMIT_CPU (CPU-time limit)
 *          A child busy-loops with a 1 second soft limit. The kernel sends
 *          SIGXCPU, which terminates it; the parent sees the signal.
 *          RLIMIT_CORE is set to 0 first so no core file is left behind.
 */
#include "partb.h"

#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define MB (1024UL * 1024UL)

static void print_limit(const char *label, int resource)
{
    struct rlimit rl;
    if (getrlimit(resource, &rl) < 0) {
        printf("  %s: getrlimit failed: %s\n", label, strerror(errno));
        return;
    }
    printf("  %s: soft=", label);
    if (rl.rlim_cur == RLIM_INFINITY) printf("unlimited"); else printf("%llu", (unsigned long long)rl.rlim_cur);
    printf(" hard=");
    if (rl.rlim_max == RLIM_INFINITY) printf("unlimited"); else printf("%llu", (unsigned long long)rl.rlim_max);
    printf("\n");
}

/* Tries to allocate n bytes and reports the outcome. Returns 1 on success. */
static int try_alloc(const char *when, size_t n)
{
    /* volatile + a touched byte stop the compiler from optimising malloc away */
    volatile char *p = malloc(n);
    if (!p) {
        printf("  [child] malloc(%lu MB) %s: FAILED (%s)\n", (unsigned long)(n / MB), when, strerror(errno));
        fflush(stdout);
        return 0;
    }
    p[0] = 1;
    printf("  [child] malloc(%lu MB) %s: succeeded\n", (unsigned long)(n / MB), when);
    fflush(stdout);
    free((void *)p);
    return 1;
}

/* Child for demo 1. Never returns. */
static void child_address_space(void)
{
    struct rlimit rl;
    int before_big, after_big, after_small;

    print_limit("[child] RLIMIT_AS before", RLIMIT_AS);
    before_big = try_alloc("before the limit", 256 * MB);

    if (getrlimit(RLIMIT_AS, &rl) < 0) { perror("  [child] getrlimit"); _exit(1); }
    rl.rlim_cur = 128 * MB;
    if (rl.rlim_max != RLIM_INFINITY && rl.rlim_max < rl.rlim_cur)
        rl.rlim_cur = rl.rlim_max;
    if (setrlimit(RLIMIT_AS, &rl) < 0) { perror("  [child] setrlimit(RLIMIT_AS)"); _exit(1); }
    printf("  [child] setrlimit(RLIMIT_AS, soft = 128 MB) applied\n");
    print_limit("[child] RLIMIT_AS after ", RLIMIT_AS);

    after_big = try_alloc("after the limit", 256 * MB);
    after_small = try_alloc("after the limit", 16 * MB);
    fflush(stdout);
    _exit((before_big && !after_big && after_small) ? 0 : 1);
}

/* Child for demo 2. Never returns. */
static void child_cpu_time(void)
{
    struct rlimit core = { 0, 0 };
    struct rlimit cpu;
    struct timespec t0, t1;
    volatile unsigned long x = 0;

    setrlimit(RLIMIT_CORE, &core);              /* best effort: no core file on SIGXCPU */

    if (getrlimit(RLIMIT_CPU, &cpu) < 0) { perror("  [child] getrlimit"); _exit(1); }
    cpu.rlim_cur = 1;
    if (cpu.rlim_max == RLIM_INFINITY || cpu.rlim_max > 2)
        cpu.rlim_max = 2;                       /* hard limit: SIGKILL if SIGXCPU is ignored */
    if (setrlimit(RLIMIT_CPU, &cpu) < 0) { perror("  [child] setrlimit(RLIMIT_CPU)"); _exit(1); }
    printf("  [child] setrlimit(RLIMIT_CPU, soft = 1 s, hard = 2 s) applied\n");
    print_limit("[child] RLIMIT_CPU now", RLIMIT_CPU);
    printf("  [child] busy-looping; the kernel should stop this process after ~1 CPU-second\n");
    fflush(stdout);

    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (;;) {
        x++;
        if ((x & 0xFFFFFUL) == 0) {             /* safety net: never loop forever */
            clock_gettime(CLOCK_MONOTONIC, &t1);
            if (t1.tv_sec - t0.tv_sec > 8) {
                printf("  [child] limit was NOT enforced - giving up\n");
                fflush(stdout);
                _exit(2);
            }
        }
    }
}

/* Forks, runs one demo child, and decodes its exit status. */
static pid_t spawn(void (*body)(void))
{
    fflush(stdout);
    pid_t pid = fork();
    if (pid < 0) {
        fprintf(stderr, "rlimit: fork: %s\n", strerror(errno));
        return -1;
    }
    if (pid == 0)
        body();
    return pid;
}

static int reap(pid_t pid, status_kind_t *kind, int *value, char *desc, size_t len)
{
    int st;
    while (waitpid(pid, &st, 0) < 0) {
        if (errno != EINTR) {
            fprintf(stderr, "rlimit: waitpid: %s\n", strerror(errno));
            return -1;
        }
    }
    *kind = describe_status(st, value, desc, len);
    return 0;
}

int run_rlimit_demo(void)
{
    int rc = 0, value;
    char desc[128];
    status_kind_t kind;
    pid_t pid;

    print_banner("Resource limits with setrlimit()");
    print_limit("parent RLIMIT_AS (unchanged by the demos)", RLIMIT_AS);

    printf("\n-- Demo 1: RLIMIT_AS (address-space limit) in a child process --\n");
    pid = spawn(child_address_space);
    if (pid < 0 || reap(pid, &kind, &value, desc, sizeof desc) < 0) return 1;
    printf("parent: child %d %s\n", (int)pid, desc);
    if (kind == ST_EXIT_OK)
        printf("observed effect: allocation of 256 MB failed once the 128 MB limit was set; a 16 MB allocation still worked\n");
    else {
        printf("unexpected result in demo 1\n");
        rc = 1;
    }

    printf("\n-- Demo 2: RLIMIT_CPU (CPU-time limit) in a child process --\n");
    pid = spawn(child_cpu_time);
    if (pid < 0 || reap(pid, &kind, &value, desc, sizeof desc) < 0) return 1;
    printf("parent: child %d %s\n", (int)pid, desc);
    if (kind == ST_SIGNALED && (value == SIGXCPU || value == SIGKILL))
        printf("observed effect: the kernel stopped the busy-looping child when it used up its CPU-time limit\n");
    else {
        printf("unexpected result in demo 2\n");
        rc = 1;
    }

    printf("\n");
    print_limit("parent RLIMIT_AS (still unchanged)", RLIMIT_AS);
    printf("RESULT: %s\n", rc == 0 ? "PASS - both limits were applied and their effects observed"
                                    : "FAIL - a limit did not behave as expected");
    return rc;
}
