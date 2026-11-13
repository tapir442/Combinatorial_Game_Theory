/* Witness depth of the Officers (.6) rare-value check, for the FPGA split check.
 *
 *   witness_depth N [N0]        (defaults N0 = 50000)
 *
 * G(n) for n < N0 by brute force; from N0 on with the rare-value method as in
 * octal --rare: step 1 marks G(r) ^ G(n-1-r) for the rare heaps r, cval = smallest
 * unmarked common value; step 2 scans the pairs (a, n-1-a), a = 1, 2, ... until
 * every rare value below cval is reached.  The witness depth of n is the a at which
 * the last one is found (0 if step 1 already reached all of them).  If the scan
 * runs out, G(n) is a new rare value (reported; never seen for .6).
 *
 * Prints, per decade of n and overall: heaps, mean depth, max depth (and where),
 * and the number of heaps with depth > K for K = 100 .. 16384.  The last line is the
 * CRC-32 (zlib, values as u16 LE) of G(N0 .. N-1), comparable with run_board.py.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int is_rare(unsigned v) { return !(__builtin_popcount(v & 0x1EE) & 1); }

static uint32_t crc_tab[256];
static void crc_init(void) {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++) c = c & 1 ? (c >> 1) ^ 0xEDB88320u : c >> 1;
        crc_tab[i] = c;
    }
}
static uint32_t crc_u16(uint32_t c, unsigned v) {
    c = crc_tab[(c ^ v) & 0xFF] ^ (c >> 8);
    return crc_tab[(c ^ (v >> 8)) & 0xFF] ^ (c >> 8);
}

static const long KS[] = {100, 200, 500, 1000, 2000, 4096, 8192, 16384};
#define NK (int)(sizeof KS / sizeof KS[0])

typedef struct {
    long heaps, max, max_n, over[NK];
    double sum;
} Stat;

static void add(Stat *s, long n, long d) {
    s->heaps++;
    s->sum += d;
    if (d > s->max) { s->max = d; s->max_n = n; }
    for (int i = 0; i < NK; i++) s->over[i] += d > KS[i];
}

static void print(const char *label, const Stat *s) {
    printf("%-22s %12ld %8.1f %6ld %14ld", label, s->heaps, s->heaps ? s->sum / s->heaps : 0.0, s->max, s->max_n);
    for (int i = 0; i < NK; i++) printf(" %11ld", s->over[i]);
    printf("\n");
    fflush(stdout);
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: witness_depth N [N0]\n"); return 2; }
    long N = (long)atof(argv[1]), N0 = argc > 2 ? (long)atof(argv[2]) : 50000;
    uint16_t *G = malloc((size_t)N * sizeof *G);
    long *rare = malloc((size_t)N0 * sizeof *rare);
    if (!G || !rare) { perror("malloc"); return 1; }
    crc_init();
    static uint32_t seen[1024];
    uint32_t stamp = 0;

    /* brute force below N0 */
    long nrare = 0;
    for (long n = 0; n < N0 && n < N; n++) {
        stamp++;
        for (long a = 0; n >= 2 && 2 * a <= n - 1; a++) seen[G[a] ^ G[n - 1 - a]] = stamp;
        unsigned v = 0;
        while (seen[v] == stamp) v++;
        G[n] = (uint16_t)v;
        if (is_rare(v)) rare[nrare++] = n;
    }
    printf("brute force to %ld: %ld rare heaps, last %ld\n", N0, nrare, nrare ? rare[nrare - 1] : -1);
    printf("%-22s %12s %8s %6s %14s", "range", "heaps", "mean", "max", "at n");
    for (int i = 0; i < NK; i++) printf("     d>%-5ld", KS[i]);
    printf("\n");

    Stat all = {0}, dec = {0};
    long dec_lo = N0, dec_hi = 1;
    while (dec_hi <= N0) dec_hi *= 10;
    uint32_t crc = 0xFFFFFFFFu;
    for (long n = N0; n < N; n++) {
        if (n == dec_hi) {
            char l[64];
            snprintf(l, sizeof l, "%.0e .. %.0e", (double)dec_lo, (double)dec_hi);
            print(l, &dec);
            memset(&dec, 0, sizeof dec);
            dec_lo = dec_hi;
            dec_hi *= 10;
        }
        stamp++;
        long m = n - 1;
        /* step 1: rare heaps (the small side of the pair; all rare heaps are < N0 <= n/2 here...
         * checked: r <= m - r) */
        for (long i = 0; i < nrare && rare[i] <= m - rare[i]; i++) seen[G[rare[i]] ^ G[m - rare[i]]] = stamp;
        seen[G[0] ^ G[m]] = stamp;               /* a = 0: one heap left (G(0) = 0 is rare) */
        unsigned cval = 0;
        while (seen[cval] == stamp || is_rare(cval)) cval++;
        int need = 0;
        for (unsigned v = 0; v < cval; v++) need += is_rare(v) && seen[v] != stamp;
        long depth = 0;
        unsigned g = cval;
        if (need) {
            long a;
            for (a = 1; 2 * a <= m; a++) {
                unsigned x = G[a] ^ G[m - a];
                if (x < cval && seen[x] != stamp) {
                    seen[x] = stamp;
                    if (--need == 0) break;
                }
            }
            if (need) {                          /* every option marked: plain mex */
                g = 0;
                while (seen[g] == stamp) g++;
                printf("NEW RARE VALUE: G(%ld) = %u\n", n, g);
                rare[nrare++] = n;               /* (array sized N0: stop if it overflows) */
                if (nrare == N0) return 1;
                depth = m / 2;
            } else depth = a;
        }
        G[n] = (uint16_t)g;
        crc = crc_u16(crc, g);
        add(&all, n, depth);
        add(&dec, n, depth);
    }
    char l[64];
    snprintf(l, sizeof l, "%.0e .. %.0e", (double)dec_lo, (double)N);
    print(l, &dec);
    print("all", &all);
    printf("crc of G(%ld .. %ld): %08x\n", N0, N - 1, ~crc);
    return 0;
}
