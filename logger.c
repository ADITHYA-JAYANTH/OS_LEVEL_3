/*
 * logger.c - FR3: concurrent activity logger.
 *
 * Bounded producer-consumer queue built on POSIX threads:
 *   - one mutex protects the circular buffer and all counters,
 *   - condition variable not_full  : producers wait while the queue is full,
 *   - condition variable not_empty : the consumer waits while it is empty.
 * Several producer threads generate scheduler-style events; one consumer
 * thread writes them to LOG_FILE. The consumer is deliberately slower than the
 * producers so both the "queue full" and "queue empty" cases are visible.
 *
 * Termination: each producer counts itself in producers_done when it has
 * generated all its events. The consumer exits only when the queue is empty
 * AND all producers are done, so no event is lost. main joins every thread.
 */
#include "partb.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#define LOG_QCAP            5     /* queue capacity (small, so it fills up) */
#define N_PRODUCERS         3
#define EVENTS_PER_PRODUCER 6
#define CONSUMER_DELAY_MS   20    /* simulated disk-write time per entry */

typedef struct {
    int    seq;                   /* global sequence number, set on enqueue */
    int    producer;
    time_t ts;
    char   msg[80];
} log_entry_t;

typedef struct {
    log_entry_t buf[LOG_QCAP];
    int in, out, count;           /* circular buffer state */
    int next_seq;
    int producers_expected;       /* producer threads actually started */
    int producers_done;
    int produced, consumed, written;
    int full_waits, empty_waits;
    int write_errors;
    pthread_mutex_t lock;
    pthread_cond_t  not_full;
    pthread_cond_t  not_empty;
    FILE *fp;
} log_queue_t;

typedef struct {
    log_queue_t *q;
    int id;
} producer_arg_t;

static const char *const EVENTS[EVENTS_PER_PRODUCER] = {
    "submitted to the job queue",
    "admitted by the scheduler",
    "started running on the CPU",
    "resource request checked (safe)",
    "time quantum expired, re-queued",
    "completed and released resources"
};

static void *producer_thread(void *arg)
{
    producer_arg_t *pa = arg;
    log_queue_t *q = pa->q;

    for (int i = 0; i < EVENTS_PER_PRODUCER; i++) {
        log_entry_t e;
        memset(&e, 0, sizeof e);
        e.producer = pa->id;
        e.ts = time(NULL);
        snprintf(e.msg, sizeof e.msg, "job J%d%02d %s", pa->id, i + 1, EVENTS[i]);

        pthread_mutex_lock(&q->lock);
        while (q->count == LOG_QCAP) {          /* while, not if: guards against spurious wakeups */
            q->full_waits++;
            printf("  [producer %d] queue FULL (%d/%d) - waiting\n", pa->id, q->count, LOG_QCAP);
            pthread_cond_wait(&q->not_full, &q->lock);
        }
        e.seq = ++q->next_seq;
        q->buf[q->in] = e;
        q->in = (q->in + 1) % LOG_QCAP;
        q->count++;
        q->produced++;
        printf("  [producer %d] produced seq %02d (queue %d/%d)\n", pa->id, e.seq, q->count, LOG_QCAP);
        pthread_cond_signal(&q->not_empty);
        pthread_mutex_unlock(&q->lock);

        msleep(5L * pa->id);                    /* producers run at different speeds */
    }

    pthread_mutex_lock(&q->lock);
    q->producers_done++;
    printf("  [producer %d] finished all %d events\n", pa->id, EVENTS_PER_PRODUCER);
    if (q->producers_done >= q->producers_expected)
        pthread_cond_broadcast(&q->not_empty);  /* wake the consumer so it can exit */
    pthread_mutex_unlock(&q->lock);
    return NULL;
}

static void *consumer_thread(void *arg)
{
    log_queue_t *q = arg;

    for (;;) {
        log_entry_t e;

        pthread_mutex_lock(&q->lock);
        while (q->count == 0 && q->producers_done < q->producers_expected) {
            q->empty_waits++;
            printf("  [consumer]   queue EMPTY (0/%d) - waiting\n", LOG_QCAP);
            pthread_cond_wait(&q->not_empty, &q->lock);
        }
        if (q->count == 0) {                    /* empty and every producer is done */
            pthread_mutex_unlock(&q->lock);
            break;
        }
        e = q->buf[q->out];
        q->out = (q->out + 1) % LOG_QCAP;
        q->count--;
        q->consumed++;
        printf("  [consumer]   took seq %02d from producer %d (queue %d/%d)\n",
               e.seq, e.producer, q->count, LOG_QCAP);
        pthread_cond_signal(&q->not_full);
        pthread_mutex_unlock(&q->lock);

        /* File I/O happens outside the lock so producers are not blocked by it.
         * Only this thread touches q->fp, so no extra locking is needed. */
        char stamp[32];
        struct tm tmv;
        localtime_r(&e.ts, &tmv);
        strftime(stamp, sizeof stamp, "%Y-%m-%d %H:%M:%S", &tmv);
        if (fprintf(q->fp, "[%s] seq=%02d producer=%d event=\"%s\"\n",
                    stamp, e.seq, e.producer, e.msg) < 0 || fflush(q->fp) != 0)
            q->write_errors++;
        else
            q->written++;
        msleep(CONSUMER_DELAY_MS);
    }
    pthread_mutex_lock(&q->lock);
    printf("  [consumer]   queue drained and producers done - exiting\n");
    pthread_mutex_unlock(&q->lock);
    return NULL;
}

int run_logger_demo(void)
{
    static log_queue_t q;                       /* static: zero-initialised */
    pthread_t producers[N_PRODUCERS], consumer;
    producer_arg_t args[N_PRODUCERS];
    int started = 0, consumer_started = 0, rc = 1, err;

    print_banner("Producer-consumer logger (pthreads)");
    printf("queue capacity %d, %d producers x %d events, log file '%s'\n",
           LOG_QCAP, N_PRODUCERS, EVENTS_PER_PRODUCER, LOG_FILE);

    memset(&q, 0, sizeof q);
    q.producers_expected = N_PRODUCERS;
    q.fp = fopen(LOG_FILE, "w");
    if (!q.fp) {
        fprintf(stderr, "logger: cannot open '%s': %s\n", LOG_FILE, strerror(errno));
        return 1;
    }
    fprintf(q.fp, "# CampusCompute event log: [time] seq producer event\n");

    if ((err = pthread_mutex_init(&q.lock, NULL)) != 0) {
        fprintf(stderr, "logger: pthread_mutex_init: %s\n", strerror(err));
        fclose(q.fp);
        return 1;
    }
    if ((err = pthread_cond_init(&q.not_full, NULL)) != 0 ||
        (err = pthread_cond_init(&q.not_empty, NULL)) != 0) {
        fprintf(stderr, "logger: pthread_cond_init: %s\n", strerror(err));
        pthread_mutex_destroy(&q.lock);
        fclose(q.fp);
        return 1;
    }

    /* Start the consumer first; without it the producers would block forever. */
    if ((err = pthread_create(&consumer, NULL, consumer_thread, &q)) != 0) {
        fprintf(stderr, "logger: cannot start consumer: %s\n", strerror(err));
        goto out;
    }
    consumer_started = 1;

    for (int i = 0; i < N_PRODUCERS; i++) {
        args[i].q = &q;
        args[i].id = i + 1;
        if ((err = pthread_create(&producers[i], NULL, producer_thread, &args[i])) != 0) {
            fprintf(stderr, "logger: cannot start producer %d: %s\n", i + 1, strerror(err));
            break;
        }
        started++;
    }
    if (started != N_PRODUCERS) {               /* tell the consumer how many to wait for */
        pthread_mutex_lock(&q.lock);
        q.producers_expected = started;
        pthread_cond_broadcast(&q.not_empty);
        pthread_mutex_unlock(&q.lock);
    }

    for (int i = 0; i < started; i++) {
        pthread_join(producers[i], NULL);
        printf("main: joined producer thread %d\n", i + 1);
    }
    pthread_join(consumer, NULL);
    consumer_started = 0;
    printf("main: joined consumer thread - all logger threads have terminated\n");

    printf("\nSummary: produced=%d consumed=%d written-to-file=%d "
           "(producer waits on full: %d, consumer waits on empty: %d)\n",
           q.produced, q.consumed, q.written, q.full_waits, q.empty_waits);
    if (started == N_PRODUCERS && q.produced == N_PRODUCERS * EVENTS_PER_PRODUCER &&
        q.consumed == q.produced && q.written == q.produced && q.write_errors == 0) {
        printf("RESULT: PASS - no events lost; log written to %s\n", LOG_FILE);
        rc = 0;
    } else {
        printf("RESULT: FAIL - event counts do not match or a write failed\n");
    }

out:
    if (consumer_started)                       /* only reached if producer setup failed early */
        pthread_join(consumer, NULL);
    pthread_cond_destroy(&q.not_full);
    pthread_cond_destroy(&q.not_empty);
    pthread_mutex_destroy(&q.lock);
    if (fclose(q.fp) != 0) {
        fprintf(stderr, "logger: error closing '%s': %s\n", LOG_FILE, strerror(errno));
        rc = 1;
    }
    return rc;
}
