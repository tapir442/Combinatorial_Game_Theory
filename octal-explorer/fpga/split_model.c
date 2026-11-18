/* =============================================================================
 * split_model.c -- model of an on-board split check for Officers (.6)
 *
 * Question: how fast could the Nexys A7 *verify* G(n) (not only speculate it),
 * and which hardware layout does it best?  The program recomputes G(n) exactly
 * as octal --rare does, records for every heap how much checking work it needs,
 * and feeds that work into three layouts.
 *
 *   gcc -O3 -march=native -Wall -o build/split_model split_model.c
 *   build/split_model N [N0]        (defaults N0 = 50000; 1e6 ~7 s, 1e8 ~11 min)
 *
 * Memory: 2 bytes per heap (G as uint16), so 1e8 needs 200 MB.
 *
 * -----------------------------------------------------------------------------
 * The check of one heap
 * -----------------------------------------------------------------------------
 * Heap n, m = n - 1; its options are the pairs (a, m - a), 0 <= a <= m/2, with
 * value G(a) ^ G(m - a).  As in octal --rare (Grossman 2017):
 *
 *   step 1  mark G(r) ^ G(m - r) for the rare heaps r (all below N0) and a = 0;
 *           cval = the smallest *common* value not marked = the speculated g.
 *   step 2  every *rare-class* value below cval must be reached too.  Scan the
 *           pairs a = 1, 2, ... until the last one is found.
 *
 * "need"  = number of rare-class values below cval not reached in step 1.
 * "depth" = the a at which step 2 finds the last of them (the witness depth).
 *
 * For n >= N0 step 1 never reaches a rare-class value (counted below as
 * "rare marks from step 1"; 0 in every run so far): G(r) is rare and G(m - r)
 * is common, and rare ^ common = common.  So the board's check is all of step 2,
 * and its work per heap is exactly "depth" pairs.
 *
 * -----------------------------------------------------------------------------
 * The three layouts
 * -----------------------------------------------------------------------------
 * A  taps: K fixed pairs a = 1 .. K, evaluated for every heap, like the taps of
 *    the speculation core.  Heaps with depth > K are flagged and their pairs
 *    K+1 .. depth must come from elsewhere (DDR2).  Throughput: with D pair
 *    decoders, D / K heaps per clock, whatever the depth.
 *
 * B  free lanes: L lanes, each takes the next heap from a FIFO and streams W
 *    pairs per clock out of block RAM (a constant prefix G(1 ..) and a window of
 *    recent values), stopping at the last witness: ceil(depth / W) + OV clocks.
 *    The speculation core delivers one heap every PS clocks and stalls when the
 *    FIFO (Q heaps) is full.  Event-driven simulation; reports heaps per clock,
 *    lane load, and the window span the lanes need (see lanes_add).
 *    Upper bound: each lane needs its own RAM ports, which do not fit (135 BRAMs).
 *
 * C  lockstep groups (what check_group.sv implements): GS lanes check GS
 *    consecutive heaps together and share the RAM reads, so the group runs
 *    until its deepest heap is done.  Reported: the mean group maximum and the
 *    efficiency = mean depth / mean group maximum, the factor by which C is
 *    slower than B with the same number of decoders.
 *
 * The last line of witness_depth.c and this program's "crc" line agree: CRC-32
 * (zlib) of G(N0 .. N-1) as u16 LE, the same as run_board.py's CRC.
 * =============================================================================
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Rare class of .6: popcount(v & 0x1EE) even (bits 1, 2, 3, 5, 6, 7, 8). */
static int is_rare(unsigned v) { return !(__builtin_popcount(v & 0x1EE) & 1); }

/* ---- CRC-32 (zlib polynomial, reflected), fed 2 bytes per value ---- */
static uint32_t crc_tab[256];
static void crc_init(void) {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++) c = c & 1 ? (c >> 1) ^ 0xEDB88320u : c >> 1;
        crc_tab[i] = c;
    }
}
static uint32_t crc_u16(uint32_t c, unsigned v) {
    c = crc_tab[(c ^ v) & 0xFF] ^ (c >> 8);          /* low byte first (LE) */
    return crc_tab[(c ^ (v >> 8)) & 0xFF] ^ (c >> 8); /* then the high byte */
}

/* =============================================================================
 * Layout A: fixed taps.  KA also serves as the list of window sizes for B.
 * ============================================================================= */
static const long KA[] = {4096, 8192, 16384, 32768, 65536, 131072, 262144};
#define NKA (int)(sizeof KA / sizeof KA[0])
static long a_flag[NKA];             /* heaps with depth > K */
static double a_tail_pairs[NKA];     /* sum of (depth - K) over them: the DDR2 scan work */

/* =============================================================================
 * Layout B: free lanes, event-driven.  Time is in clocks; nothing is simulated
 * per clock, only per heap: when the core emits it, when a lane starts it, and
 * when that lane is free again.
 * ============================================================================= */
#define OV 4                         /* clocks per heap to start a lane (load g, set up the read) */
#define Q 64                         /* FIFO between the speculation core and the lanes */
typedef struct {
    int W, L, PS;                    /* pairs/clock per lane, lanes, core clocks per heap */
    double *free_at;                 /* L lane free times, a min-heap: free_at[0] is the earliest */
    double fifo_start[Q];            /* start times of the last Q heaps (ring by heap index) */
    double emit;                     /* time the core emitted the previous heap */
    double busy;                     /* sum of lane clocks over all heaps */
    long span_max, span_max_n;       /* largest window span and the heap that needed it */
    long span_over[NKA];             /* heaps whose span exceeds KA[k] */
    long heaps;                      /* heaps seen */
} Lanes;

/* Restore the min-heap property after free_at[i] grew (sift down). */
static void heap_down(double *h, int L, int i) {
    for (;;) {
        int l = 2 * i + 1, r = l + 1, s = i;
        if (l < L && h[l] < h[s]) s = l;
        if (r < L && h[r] < h[s]) s = r;
        if (s == i) return;
        double t = h[i]; h[i] = h[s]; h[s] = t;
        i = s;
    }
}

/* One heap n of depth d through layout B. */
static void lanes_add(Lanes *B, long n, long d) {
    long i = B->heaps++;
    /* Emit: PS clocks after the previous heap, unless the FIFO is full.  The
     * FIFO holds heaps i-Q+1 .. i, so heap i can only enter once heap i-Q has
     * left it, i.e. has started on a lane: fifo_start[i % Q] still holds that
     * start time (it is overwritten below). */
    double e = B->emit + B->PS;
    if (i >= Q && B->fifo_start[i % Q] > e) e = B->fifo_start[i % Q];
    B->emit = e;
    /* Start: on the earliest free lane, not before the heap exists.  Heaps are
     * started in order, so taking the minimum of the free times is exact. */
    double s = B->free_at[0] > e ? B->free_at[0] : e;
    long c = (d + B->W - 1) / B->W + OV;             /* lane clocks for this heap */
    double f = s + c;
    B->free_at[0] = f;                               /* that lane is busy until f */
    heap_down(B->free_at, B->L, 0);
    B->fifo_start[i % Q] = s;
    B->busy += c;
    /* Window span: the oldest value the lane reads is G(m - d); by the time it
     * reads it (at f at the latest) the core has written about (f - e) / PS more
     * heaps.  A window RAM of at least `span` values therefore still holds it.
     * (f - e) / PS is a lower bound on what the core produced -- it may have run
     * ahead into the FIFO -- so span is accurate to within Q. */
    long span = (long)((f - e) / B->PS) + 1 + d;
    if (span > B->span_max) { B->span_max = span; B->span_max_n = n; }
    for (int k = 0; k < NKA; k++) B->span_over[k] += span > KA[k];
}

/* =============================================================================
 * Layout C: lockstep groups.  The heaps are cut into aligned blocks of GS
 * consecutive heaps; each block costs its largest depth.  (A partial last block
 * is left out; that changes nothing at the sizes we run.)
 * ============================================================================= */
static const int GSZ[] = {1, 2, 4, 8, 16, 32};
#define NG (int)(sizeof GSZ / sizeof GSZ[0])
static double g_sum[NG];             /* sum of block maxima */
static long g_cur[NG], g_cnt[NG];    /* running maximum and size of the current block */

/* The B configurations: every W, total decoders L * W, and core pace PS. */
static const int BW[] = {1, 2, 4, 8};
static const int BLW[] = {32, 64, 96, 128, 192};      /* L * W: pair decoders in all lanes */
static const int BPS[] = {8, 16, 24, 32};             /* 8 = the folded core now */

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: split_model N [N0]\n"); return 2; }
    long N = (long)atof(argv[1]), N0 = argc > 2 ? (long)atof(argv[2]) : 50000;
    uint16_t *G = malloc((size_t)N * sizeof *G);     /* all values: the scan reads G(m - a) far back */
    long *rare = malloc((size_t)N0 * sizeof *rare);  /* rare heaps, ascending (all < N0 for .6) */
    if (!G || !rare) { perror("malloc"); return 1; }
    crc_init();
    /* seen[v] == stamp: v is marked for the current heap.  Bumping stamp per
     * heap clears all marks in O(1).  1024 > the largest XOR of two 9-bit values. */
    static uint32_t seen[1024];
    uint32_t stamp = 0;

    /* ---- G(0 .. N0-1) by brute force: mex over all pairs ---- */
    long nrare = 0;
    for (long n = 0; n < N0 && n < N; n++) {
        stamp++;
        for (long a = 0; n >= 2 && 2 * a <= n - 1; a++) seen[G[a] ^ G[n - 1 - a]] = stamp;
        unsigned v = 0;
        while (seen[v] == stamp) v++;
        G[n] = (uint16_t)v;
        if (is_rare(v)) rare[nrare++] = n;
    }

    /* ---- set up the B configurations (4 * 5 * 4 = 80) ---- */
    int nB = 0;
    Lanes B[sizeof BW / sizeof *BW * sizeof BLW / sizeof *BLW * sizeof BPS / sizeof *BPS];
    for (unsigned w = 0; w < sizeof BW / sizeof *BW; w++)
        for (unsigned l = 0; l < sizeof BLW / sizeof *BLW; l++)
            for (unsigned p = 0; p < sizeof BPS / sizeof *BPS; p++) {
                Lanes *b = &B[nB++];
                memset(b, 0, sizeof *b);
                b->W = BW[w]; b->L = BLW[l] / BW[w]; b->PS = BPS[p];
                b->free_at = calloc((size_t)b->L, sizeof(double));   /* all lanes free at 0 */
            }

    /* ---- G(N0 .. N-1) with the rare-value method, recording need and depth ---- */
    long heaps = 0, need_max = 0, need_hist[300] = {0}, d_max = 0, d_max_n = 0, rare_seen = 0;
    double need_sum = 0, d_sum = 0;
    uint32_t crc = 0xFFFFFFFFu;
    for (long n = N0; n < N; n++) {
        stamp++;
        long m = n - 1;
        /* step 1: the rare heaps r on the small side (r <= m - r) */
        for (long i = 0; i < nrare && rare[i] <= m - rare[i]; i++) {
            unsigned x = G[rare[i]] ^ G[m - rare[i]];
            if (is_rare(x)) rare_seen++;                 /* expected never (see header) */
            seen[x] = stamp;
        }
        seen[G[0] ^ G[m]] = stamp;                       /* a = 0: G(0) = 0 is a rare heap too */
        /* the speculated value: smallest unmarked common value */
        unsigned cval = 0;
        while (seen[cval] == stamp || is_rare(cval)) cval++;
        /* need: rare-class values below cval still unmarked */
        int need = 0;
        for (unsigned v = 0; v < cval; v++) need += is_rare(v) && seen[v] != stamp;
        long depth = 0;
        unsigned g = cval;
        if (need) {
            /* step 2: scan a = 1, 2, ...  Every common value below cval is marked
             * already, so "x < cval and unmarked" means a missing rare-class value. */
            int left = need;
            long a;
            for (a = 1; 2 * a <= m; a++) {
                unsigned x = G[a] ^ G[m - a];
                if (x < cval && seen[x] != stamp) {
                    seen[x] = stamp;
                    if (--left == 0) break;
                }
            }
            /* Ran out of pairs: G(n) would be a new rare value.  Never seen for .6;
             * the layouts above do not cover it, so stop rather than guess. */
            if (left) { printf("NEW RARE VALUE at n = %ld: model does not cover it\n", n); return 1; }
            depth = a;
        }
        G[n] = (uint16_t)g;
        crc = crc_u16(crc, g);

        /* ---- statistics ---- */
        heaps++;
        need_sum += need;
        if (need > need_max) need_max = need;
        need_hist[need < 299 ? need : 299]++;
        d_sum += depth;
        if (depth > d_max) { d_max = depth; d_max_n = n; }
        /* A: flagged heaps and the work left beyond K */
        for (int k = 0; k < NKA; k++)
            if (depth > KA[k]) { a_flag[k]++; a_tail_pairs[k] += depth - KA[k]; }
        /* B: every configuration sees the same heap stream */
        for (int b = 0; b < nB; b++) lanes_add(&B[b], n, depth);
        /* C: close a block every GS heaps */
        for (int k = 0; k < NG; k++) {
            if (depth > g_cur[k]) g_cur[k] = depth;
            if (++g_cnt[k] == GSZ[k]) { g_sum[k] += g_cur[k]; g_cur[k] = 0; g_cnt[k] = 0; }
        }
    }

    /* ---- report ---- */
    printf("heaps %ld (n = %ld .. %ld), crc %08x\n", heaps, N0, N - 1, ~crc);
    printf("rare marks from step 1: %ld (0 means step 2 is the whole check)\n", rare_seen);
    printf("need (rare values below g): mean %.1f, max %ld\n", need_sum / heaps, need_max);
    printf("depth: mean %.1f, max %ld at n = %ld\n\n", d_sum / heaps, d_max, d_max_n);

    printf("A  taps K: flagged share, mean tail pairs per heap (DDR scan beyond K)\n");
    for (int k = 0; k < NKA; k++)
        printf("   K %7ld: flagged %.6f%%  tail pairs/heap %.3f\n", KA[k],
               100.0 * a_flag[k] / heaps, a_tail_pairs[k] / heaps);

    /* Columns: heaps per clock overall (heaps / time of the last emit), the same
     * in M heaps/s at 200 MHz, lane load (busy clocks / available lane clocks),
     * the largest window span, and the share of heaps whose span exceeds each
     * window size KA[k] (they would need the DDR2 fallback). */
    printf("\nB  lanes (OV %d, FIFO %d): W pairs/clock/lane, L lanes, PS clocks/heap of the core\n", OV, Q);
    printf("   %2s %4s %4s %3s  %10s %8s %8s  %10s", "W", "L", "L*W", "PS", "heaps/clk", "M/s@200", "load", "span max");
    for (int k = 0; k < NKA; k++) printf("  >%-7ld", KA[k]);
    printf("\n");
    for (int b = 0; b < nB; b++) {
        Lanes *x = &B[b];
        double hpc = x->heaps / x->emit;
        printf("   %2d %4d %4d %3d  %10.5f %8.2f %8.3f  %10ld", x->W, x->L, x->W * x->L, x->PS, hpc, hpc * 200,
               x->busy / (x->emit * x->L), x->span_max);
        for (int k = 0; k < NKA; k++) printf("  %-8.2e", (double)x->span_over[k] / x->heaps);
        printf("\n");
    }
    printf("\nC  lockstep groups: GS, mean group max depth, efficiency (mean depth / it)\n");
    for (int k = 0; k < NG; k++) {
        double gm = g_sum[k] / (heaps / GSZ[k]);     /* complete blocks only */
        printf("   GS %2d: %9.1f  %.3f\n", GSZ[k], gm, d_sum / heaps / gm);
    }
    /* need -> number of heaps (only the values that occur) */
    printf("\nneed histogram (need: heaps):");
    for (int v = 0; v < 300; v++) if (need_hist[v]) printf(" %d:%ld", v, need_hist[v]);
    printf("\n");
    return 0;
}
