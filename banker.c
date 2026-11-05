/*
 * banker.c - FR5: Banker's safety algorithm.
 *
 *   Need = Maximum - Allocation
 *   A state is SAFE if the processes can be ordered so each can obtain its
 *   remaining Need from Work (= Available + resources freed by earlier
 *   finishers). Otherwise it is UNSAFE.
 *
 * State file format (whitespace separated, '#' starts a comment):
 *   n m
 *   Available[0..m-1]
 *   Allocation  (n rows of m values)
 *   Maximum     (n rows of m values)
 */
#include "campuscompute.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_P 16
#define MAX_R 8
#define MAX_TOKENS (2 + MAX_R + 2 * MAX_P * MAX_R)

typedef struct {
    int n, m;
    int avail[MAX_R];
    int alloc[MAX_P][MAX_R];
    int max[MAX_P][MAX_R];
    int need[MAX_P][MAX_R];
} banker_t;

static void print_vec(const int *v, int m)
{
    printf("[");
    for (int j = 0; j < m; j++) printf(j ? " %d" : "%d", v[j]);
    printf("]");
}

static void print_matrix(const char *title, int a[][MAX_R], int n, int m)
{
    printf("%s\n      ", title);
    for (int j = 0; j < m; j++) printf(" R%-2d", j);
    printf("\n");
    for (int i = 0; i < n; i++) {
        printf("  P%-3d", i);
        for (int j = 0; j < m; j++) printf(" %3d ", a[i][j]);
        printf("\n");
    }
}

static void compute_need(banker_t *b)
{
    for (int i = 0; i < b->n; i++)
        for (int j = 0; j < b->m; j++)
            b->need[i][j] = b->max[i][j] - b->alloc[i][j];
}

/* Built-in test cases (classic textbook data). The algorithm decides the outcome. */
static void builtin_state(int safe, banker_t *b)
{
    static const int alloc[5][3] = { {0,1,0}, {2,0,0}, {3,0,2}, {2,1,1}, {0,0,2} };
    static const int max[5][3]   = { {7,5,3}, {3,2,2}, {9,0,2}, {2,2,2}, {4,3,3} };
    memset(b, 0, sizeof *b);
    b->n = 5;
    b->m = 3;
    for (int i = 0; i < 5; i++)
        for (int j = 0; j < 3; j++) {
            b->alloc[i][j] = alloc[i][j];
            b->max[i][j] = max[i][j];
        }
    static const int avail_safe[3]   = { 3, 3, 2 };
    static const int avail_unsafe[3] = { 2, 2, 1 };
    memcpy(b->avail, safe ? avail_safe : avail_unsafe, sizeof avail_safe);
    compute_need(b);
}

static int banker_load(const char *path, banker_t *b)
{
    FILE *fp = fopen(path, "r");
    if (!fp) {
        fprintf(stderr, "error: cannot open banker file '%s': %s\n", path, strerror(errno));
        return -1;
    }

    int vals[MAX_TOKENS], cnt = 0, lineno = 0, rc = 0;
    char line[512];

    while (rc == 0 && fgets(line, sizeof line, fp)) {
        lineno++;
        char *hash = strchr(line, '#');
        if (hash) *hash = '\0';
        char *save = NULL;
        for (char *t = strtok_r(line, " \t\r\n", &save); t; t = strtok_r(NULL, " \t\r\n", &save)) {
            char *end;
            errno = 0;
            long v = strtol(t, &end, 10);
            if (errno || end == t || *end != '\0' || v < 0 || v > 1000000) {
                fprintf(stderr, "error: %s:%d: invalid value '%s' (need non-negative integer)\n",
                        path, lineno, t);
                rc = -1;
                break;
            }
            if (cnt >= MAX_TOKENS) {
                fprintf(stderr, "error: %s: too much data\n", path);
                rc = -1;
                break;
            }
            vals[cnt++] = (int)v;
        }
    }
    fclose(fp);
    if (rc) return -1;

    if (cnt < 2 || vals[0] < 1 || vals[0] > MAX_P || vals[1] < 1 || vals[1] > MAX_R) {
        fprintf(stderr, "error: %s: header must be 'n m' with 1<=n<=%d, 1<=m<=%d\n", path, MAX_P, MAX_R);
        return -1;
    }
    memset(b, 0, sizeof *b);
    b->n = vals[0];
    b->m = vals[1];
    int expected = 2 + b->m + 2 * b->n * b->m;
    if (cnt != expected) {
        fprintf(stderr, "error: %s: expected %d numbers for n=%d m=%d, found %d\n",
                path, expected, b->n, b->m, cnt);
        return -1;
    }

    int k = 2;
    for (int j = 0; j < b->m; j++) b->avail[j] = vals[k++];
    for (int i = 0; i < b->n; i++)
        for (int j = 0; j < b->m; j++) b->alloc[i][j] = vals[k++];
    for (int i = 0; i < b->n; i++)
        for (int j = 0; j < b->m; j++) b->max[i][j] = vals[k++];

    for (int i = 0; i < b->n; i++)
        for (int j = 0; j < b->m; j++)
            if (b->alloc[i][j] > b->max[i][j]) {
                fprintf(stderr, "error: %s: P%d allocation of R%d (%d) exceeds its maximum (%d)\n",
                        path, i, j, b->alloc[i][j], b->max[i][j]);
                return -1;
            }
    compute_need(b);
    return 0;
}

static int vec_leq(const int *a, const int *b, int m)
{
    for (int j = 0; j < m; j++)
        if (a[j] > b[j]) return 0;
    return 1;
}

/* Returns 1 if safe (seq holds the safe sequence), 0 if unsafe. */
static int banker_safety(const banker_t *b, int seq[], int *seq_len)
{
    int work[MAX_R], finish[MAX_P] = {0}, count = 0, step = 0;
    memcpy(work, b->avail, sizeof work);

    printf("\nSafety algorithm trace:\n  Work = ");
    print_vec(work, b->m);
    printf("\n");

    int progress = 1;
    while (count < b->n && progress) {
        progress = 0;
        for (int i = 0; i < b->n; i++) {
            if (finish[i] || !vec_leq(b->need[i], work, b->m)) continue;
            printf("  Step %d: P%d can finish. Need ", ++step, i);
            print_vec(b->need[i], b->m);
            printf(" <= Work ");
            print_vec(work, b->m);
            for (int j = 0; j < b->m; j++) work[j] += b->alloc[i][j];
            printf(" -> Work = ");
            print_vec(work, b->m);
            printf("\n");
            finish[i] = 1;
            seq[count++] = i;
            progress = 1;
            break;                        /* restart scan from P0 each step */
        }
    }
    *seq_len = count;

    if (count == b->n) return 1;

    printf("  No further process can proceed. Work = ");
    print_vec(work, b->m);
    printf("\n  Blocked processes:\n");
    for (int i = 0; i < b->n; i++) {
        if (finish[i]) continue;
        printf("    P%d needs ", i);
        print_vec(b->need[i], b->m);
        printf(" > Work\n");
    }
    return 0;
}

int run_banker(const char *arg)
{
    banker_t b;
    int seq[MAX_P], len = 0;

    if (strcmp(arg, "safe") == 0) {
        builtin_state(1, &b);
        printf("\n=== Banker's algorithm: built-in SAFE test case ===\n");
    } else if (strcmp(arg, "unsafe") == 0) {
        builtin_state(0, &b);
        printf("\n=== Banker's algorithm: built-in UNSAFE test case ===\n");
    } else {
        if (banker_load(arg, &b) != 0) return -1;
        printf("\n=== Banker's algorithm: state from %s ===\n", arg);
    }

    printf("\n%d processes, %d resource types\nAvailable = ", b.n, b.m);
    print_vec(b.avail, b.m);
    printf("\n\n");
    print_matrix("Allocation:", b.alloc, b.n, b.m);
    printf("\n");
    print_matrix("Maximum:", b.max, b.n, b.m);
    printf("\n");
    print_matrix("Need = Maximum - Allocation:", b.need, b.n, b.m);

    int safe = banker_safety(&b, seq, &len);

    if (safe) {
        printf("\nRESULT: SAFE state. Safe sequence: <");
        for (int i = 0; i < len; i++) printf(i ? ", P%d" : "P%d", seq[i]);
        printf(">\n");
    } else {
        printf("\nRESULT: UNSAFE state. No safe sequence exists (%d of %d processes can finish).\n",
               len, b.n);
    }
    return 0;
}
