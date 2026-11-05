/*
 * ipc.c - FR4: inter-process communication with POSIX shared memory and
 * named semaphores.
 *
 * Scenario: a worker process (child) sends a batch of CPU-burst values to a
 * monitor process (parent) through shared memory; the parent replies with the
 * total and average through the same memory.
 *
 *   child : write bursts -> sem_post(REQ) -> sem_wait(ACK) -> read reply
 *   parent: sem_wait(REQ) -> read bursts -> write reply   -> sem_post(ACK)
 *
 * Two named semaphores (both start at 0) enforce this strict hand-over, so the
 * two processes never touch the shared structure at the same time. Waits use a
 * timeout so a crashed peer cannot hang the program.
 *
 * Every object created here is released before returning (munmap, sem_close,
 * sem_unlink, shm_unlink). Setting CAMPUS_KEEP_IPC=1 skips the unlink step so
 * the 'cleanup' command can be demonstrated separately.
 */
#include "partb.h"

#include <fcntl.h>
#include <semaphore.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define IPC_MAX_JOBS 8
#define IPC_WAIT_SECS 5

typedef struct {
    /* written by the child */
    int  writer_pid;
    int  n_jobs;
    int  burst[IPC_MAX_JOBS];
    char message[64];
    /* written by the parent */
    int    reply_pid;
    long   total_burst;
    double avg_burst;
} ipc_shared_t;

/* Waits on a semaphore with a timeout, retrying if interrupted by a signal.
 * Returns 0 on success, -1 (errno set) on timeout or error. */
static int wait_sem(sem_t *s, int seconds)
{
    struct timespec deadline;
    if (clock_gettime(CLOCK_REALTIME, &deadline) < 0)
        return -1;
    deadline.tv_sec += seconds;
    while (sem_timedwait(s, &deadline) < 0) {
        if (errno == EINTR)
            continue;
        return -1;
    }
    return 0;
}

int ipc_remove_objects(int verbose)
{
    static const char *const sems[] = { SEM_REQ_NAME, SEM_ACK_NAME };
    int removed = 0;

    for (size_t i = 0; i < sizeof sems / sizeof sems[0]; i++) {
        if (sem_unlink(sems[i]) == 0) {
            removed++;
            if (verbose) printf("  [cleanup] sem_unlink(%s): removed\n", sems[i]);
        } else if (errno == ENOENT) {
            if (verbose) printf("  [cleanup] sem_unlink(%s): not present\n", sems[i]);
        } else {
            fprintf(stderr, "  [cleanup] sem_unlink(%s): %s\n", sems[i], strerror(errno));
        }
    }
    if (shm_unlink(SHM_NAME) == 0) {
        removed++;
        if (verbose) printf("  [cleanup] shm_unlink(%s): removed\n", SHM_NAME);
    } else if (errno == ENOENT) {
        if (verbose) printf("  [cleanup] shm_unlink(%s): not present\n", SHM_NAME);
    } else {
        fprintf(stderr, "  [cleanup] shm_unlink(%s): %s\n", SHM_NAME, strerror(errno));
    }
    return removed;
}

/* Child process: the "worker" side of the exchange. Never returns. */
static void ipc_child(ipc_shared_t *shm, sem_t *req, sem_t *ack)
{
    static const int bursts[] = { 5, 3, 8, 6, 2 };
    int n = (int)(sizeof bursts / sizeof bursts[0]);

    shm->writer_pid = (int)getpid();
    shm->n_jobs = n;
    memcpy(shm->burst, bursts, sizeof bursts);
    snprintf(shm->message, sizeof shm->message, "CPU bursts from worker %d", (int)getpid());
    printf("  [child  %d] wrote %d burst values into shared memory\n", (int)getpid(), n);
    printf("  [child  %d] sem_post(%s): data is ready\n", (int)getpid(), SEM_REQ_NAME);
    fflush(stdout);
    if (sem_post(req) < 0) {
        perror("  [child] sem_post");
        _exit(1);
    }

    if (wait_sem(ack, IPC_WAIT_SECS) != 0) {
        fprintf(stderr, "  [child] no reply from parent: %s\n", strerror(errno));
        _exit(1);
    }
    printf("  [child  %d] reply from parent %d: total=%ld average=%.2f\n",
           (int)getpid(), shm->reply_pid, shm->total_burst, shm->avg_burst);
    fflush(stdout);
    _exit(0);
}

int run_ipc_demo(void)
{
    int rc = 1, fd = -1, reaped = 0, st = 0;
    int keep = getenv("CAMPUS_KEEP_IPC") != NULL;
    ipc_shared_t *shm = MAP_FAILED;
    sem_t *req = SEM_FAILED, *ack = SEM_FAILED;
    pid_t pid = -1;

    print_banner("Shared-memory IPC with named semaphores");

    /* Start clean: remove leftovers of a previous crashed run (our names only). */
    if (ipc_remove_objects(0) > 0)
        printf("note: removed stale IPC objects left by an earlier run\n");

    fd = shm_open(SHM_NAME, O_CREAT | O_EXCL | O_RDWR, 0600);
    if (fd < 0) {
        fprintf(stderr, "ipc: shm_open(%s): %s\n", SHM_NAME, strerror(errno));
        goto out;
    }
    if (ftruncate(fd, (off_t)sizeof *shm) < 0) {
        fprintf(stderr, "ipc: ftruncate: %s\n", strerror(errno));
        goto out;
    }
    shm = mmap(NULL, sizeof *shm, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (shm == MAP_FAILED) {
        fprintf(stderr, "ipc: mmap: %s\n", strerror(errno));
        goto out;
    }
    close(fd);                                  /* the mapping stays valid */
    fd = -1;
    memset(shm, 0, sizeof *shm);

    req = sem_open(SEM_REQ_NAME, O_CREAT | O_EXCL, 0600, 0);
    ack = req == SEM_FAILED ? SEM_FAILED : sem_open(SEM_ACK_NAME, O_CREAT | O_EXCL, 0600, 0);
    if (req == SEM_FAILED || ack == SEM_FAILED) {
        fprintf(stderr, "ipc: sem_open: %s\n", strerror(errno));
        goto out;
    }
    printf("created: shared memory %s (%zu bytes), semaphores %s and %s\n",
           SHM_NAME, sizeof *shm, SEM_REQ_NAME, SEM_ACK_NAME);

    fflush(stdout);                             /* avoid duplicated buffered output in the child */
    pid = fork();
    if (pid < 0) {
        fprintf(stderr, "ipc: fork: %s\n", strerror(errno));
        goto out;
    }
    if (pid == 0)
        ipc_child(shm, req, ack);               /* never returns */

    printf("parent %d: forked worker %d, waiting on %s\n", (int)getpid(), (int)pid, SEM_REQ_NAME);
    if (wait_sem(req, IPC_WAIT_SECS) != 0) {
        fprintf(stderr, "ipc: no data from worker: %s\n", strerror(errno));
        goto out;
    }

    /* Validate before trusting data that came from another process. */
    if (shm->n_jobs < 1 || shm->n_jobs > IPC_MAX_JOBS) {
        fprintf(stderr, "ipc: worker sent an invalid job count (%d)\n", shm->n_jobs);
        goto out;
    }
    long total = 0;
    printf("parent %d: received from pid %d: \"%s\"\n  bursts =", (int)getpid(), shm->writer_pid, shm->message);
    for (int i = 0; i < shm->n_jobs; i++) {
        printf(" %d", shm->burst[i]);
        total += shm->burst[i];
    }
    printf("\n");
    printf("parent %d: sender pid %s the forked worker\n", (int)getpid(),
           shm->writer_pid == (int)pid ? "matches" : "DOES NOT match");

    shm->reply_pid = (int)getpid();
    shm->total_burst = total;
    shm->avg_burst = (double)total / shm->n_jobs;
    printf("parent %d: wrote reply (total=%ld, average=%.2f), sem_post(%s)\n",
           (int)getpid(), total, shm->avg_burst, SEM_ACK_NAME);
    fflush(stdout);
    if (sem_post(ack) < 0) {
        fprintf(stderr, "ipc: sem_post: %s\n", strerror(errno));
        goto out;
    }

    while (waitpid(pid, &st, 0) < 0) {
        if (errno != EINTR) {
            fprintf(stderr, "ipc: waitpid: %s\n", strerror(errno));
            goto out;
        }
    }
    reaped = 1;
    {
        char desc[96];
        int v;
        status_kind_t k = describe_status(st, &v, desc, sizeof desc);
        printf("parent: worker %d %s\n", (int)pid, desc);
        if (k == ST_EXIT_OK && shm->writer_pid == (int)pid) {
            printf("RESULT: PASS - data exchanged through shared memory and synchronised by semaphores\n");
            rc = 0;
        } else {
            printf("RESULT: FAIL - worker did not finish cleanly\n");
        }
    }

out:
    if (pid > 0 && !reaped) {                   /* error path: do not leave a child behind */
        kill(pid, SIGKILL);
        waitpid(pid, NULL, 0);
    }
    if (fd >= 0) close(fd);
    if (shm != MAP_FAILED) munmap(shm, sizeof *shm);
    if (req != SEM_FAILED) sem_close(req);
    if (ack != SEM_FAILED) sem_close(ack);
    if (keep)
        printf("CAMPUS_KEEP_IPC is set: IPC objects left in place (run './campuscompute cleanup')\n");
    else
        ipc_remove_objects(1);
    return rc;
}
