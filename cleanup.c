/*
 * cleanup.c - FR7 / S20: remove and verify the resources created by this
 * program. Only objects with this program's own names are touched
 * (see partb.h); nothing else in /dev/shm or on disk is removed.
 */
#include "partb.h"

#include <dirent.h>
#include <fcntl.h>
#include <semaphore.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

/* Number of threads in this process, from /proc/self/task; -1 if unavailable. */
static int count_threads(void)
{
    DIR *d = opendir("/proc/self/task");
    struct dirent *e;
    int n = 0;
    if (!d) return -1;
    while ((e = readdir(d)) != NULL)
        if (e->d_name[0] != '.') n++;
    closedir(d);
    return n;
}

int run_cleanup(void)
{
    int rc = 0, reaped = 0, threads;
    struct stat sb;

    print_banner("Cleanup and verification");

    printf("Removing IPC objects created by this program:\n");
    ipc_remove_objects(1);

    while (waitpid(-1, NULL, WNOHANG) > 0)      /* collect any finished child we still own */
        reaped++;
    printf("child processes collected: %d\n", reaped);

    printf("\nVerification:\n");

    int fd = shm_open(SHM_NAME, O_RDONLY, 0);
    if (fd >= 0) {
        close(fd);
        printf("  shared memory %s: STILL EXISTS\n", SHM_NAME);
        rc = 1;
    } else if (errno == ENOENT) {
        printf("  shared memory %s: gone\n", SHM_NAME);
    } else {
        printf("  shared memory %s: check failed (%s)\n", SHM_NAME, strerror(errno));
        rc = 1;
    }

    const char *sems[] = { SEM_REQ_NAME, SEM_ACK_NAME };
    for (int i = 0; i < 2; i++) {
        sem_t *s = sem_open(sems[i], 0);
        if (s != SEM_FAILED) {
            sem_close(s);
            printf("  semaphore %s: STILL EXISTS\n", sems[i]);
            rc = 1;
        } else if (errno == ENOENT) {
            printf("  semaphore %s: gone\n", sems[i]);
        } else {
            printf("  semaphore %s: check failed (%s)\n", sems[i], strerror(errno));
            rc = 1;
        }
    }

    if (waitpid(-1, NULL, WNOHANG) == -1 && errno == ECHILD)
        printf("  child processes: none remaining\n");
    else {
        printf("  child processes: some are still running or not yet collected\n");
        rc = 1;
    }

    threads = count_threads();
    if (threads == 1)
        printf("  threads: only the main thread remains (logger threads were joined)\n");
    else if (threads > 1) {
        printf("  threads: %d threads still running\n", threads);
        rc = 1;
    } else
        printf("  threads: could not be checked on this system\n");

    if (stat(LOG_FILE, &sb) == 0)
        printf("  log file %s: kept as program output (%ld bytes); 'make clean' removes it\n",
               LOG_FILE, (long)sb.st_size);
    else
        printf("  log file %s: not present\n", LOG_FILE);

    printf("RESULT: %s\n", rc == 0 ? "PASS - all program-created resources are released"
                                    : "FAIL - something was left behind");
    return rc;
}
