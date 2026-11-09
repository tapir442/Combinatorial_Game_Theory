/**
 * @file thread_test.c
 * @brief Thread-safety test for the octal library: several threads run
 *        octal_extend() / octal_period() at the same time (in chunks) and the
 *        results must equal a single-threaded run.
 *
 * Built and run by `make test-threads`, with ThreadSanitizer enabled, so any
 * data race between threads is reported and fails the test.
 */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../octal.h"

#define NTHREADS 8
#define N 6000
#define CHUNK 777

static const char *CODES[NTHREADS] = {".007", ".6", ".77", ".07", "grundy", "couples", ".137", ".f6"};

/** Per-thread job: compute code[i] in chunks, then search its period. */
typedef struct {
    const char *code;
    long n;
    uint16_t *G;
    int rc;
    int prc;
    long period[2];
} Job;

static void *worker(void *arg) {
    Job *j = arg;
    j->rc = 0;
    for (long a = 0; a < j->n && j->rc == 0; a += CHUNK) {
        long b = a + CHUNK < j->n ? a + CHUNK : j->n;
        j->rc = octal_extend(j->code, j->G, a, b);
    }
    j->prc = octal_period(j->code, j->G, j->n, j->period);
    return NULL;
}

int main(void) {
    Job par[NTHREADS], seq[NTHREADS];
    pthread_t th[NTHREADS];
    for (int i = 0; i < NTHREADS; i++) {
        long n = strcmp(CODES[i], ".f6") == 0 ? 800 : N; /* hex games are O(n^3) */
        par[i] = (Job){CODES[i], n, calloc(n, sizeof(uint16_t)), 0, 0, {0, 0}};
        seq[i] = (Job){CODES[i], n, calloc(n, sizeof(uint16_t)), 0, 0, {0, 0}};
    }
    /* reference: one after another */
    for (int i = 0; i < NTHREADS; i++) worker(&seq[i]);
    /* all at once, twice (the second round re-uses freed memory) */
    for (int round = 0; round < 2; round++) {
        for (int i = 0; i < NTHREADS; i++) {
            memset(par[i].G, 0, par[i].n * sizeof(uint16_t));
            pthread_create(&th[i], NULL, worker, &par[i]);
        }
        for (int i = 0; i < NTHREADS; i++) pthread_join(th[i], NULL);
        for (int i = 0; i < NTHREADS; i++) {
            if (par[i].rc || seq[i].rc) {
                printf("FAIL %s: return code %d / %d\n", CODES[i], par[i].rc, seq[i].rc);
                return 1;
            }
            if (memcmp(par[i].G, seq[i].G, par[i].n * sizeof(uint16_t)) != 0 ||
                par[i].prc != seq[i].prc || par[i].period[0] != seq[i].period[0] ||
                par[i].period[1] != seq[i].period[1]) {
                printf("FAIL %s: parallel result differs from sequential\n", CODES[i]);
                return 1;
            }
        }
    }
    printf("thread test OK: %d threads x 2 rounds, results identical to sequential\n", NTHREADS);
    for (int i = 0; i < NTHREADS; i++) { free(par[i].G); free(seq[i].G); }
    return 0;
}
