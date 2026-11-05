/*
 * partb.h - internal definitions shared by the Part B source files
 * (logger.c, ipc.c, worker.c, limits.c, cleanup.c, integrate.c).
 * The public entry points live in campuscompute.h.
 */
#ifndef PARTB_H
#define PARTB_H

#include "campuscompute.h"

#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <time.h>

/* Names of every external object this program creates. Cleanup only ever
 * touches these names, never anything else in /dev/shm or on disk. */
#define LOG_FILE      "campuscompute.log"
#define SHM_NAME      "/campuscompute_shm"
#define SEM_REQ_NAME  "/campuscompute_sem_req"   /* child  -> parent: data ready  */
#define SEM_ACK_NAME  "/campuscompute_sem_ack"   /* parent -> child:  reply ready */

/* How a child process ended, as decoded from a waitpid() status. */
typedef enum { ST_EXIT_OK, ST_EXIT_FAIL, ST_SIGNALED, ST_OTHER } status_kind_t;

/* worker.c: decodes a waitpid() status into a readable sentence in buf.
 * *value receives the exit code (for exits) or the signal number. */
status_kind_t describe_status(int status, int *value, char *buf, size_t len);

/* ipc.c: unlinks the named shared memory and semaphores created by the IPC
 * demo. Returns how many objects were removed. verbose=1 prints each step. */
int ipc_remove_objects(int verbose);

/* Sleep for ms milliseconds (usleep() is not available in strict C11 mode). */
static inline void msleep(long ms)
{
    struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };
    while (nanosleep(&ts, &ts) == -1 && errno == EINTR)
        ;
}

static inline void print_banner(const char *title)
{
    printf("\n===== %s =====\n", title);
    fflush(stdout);
}

#endif /* PARTB_H */
