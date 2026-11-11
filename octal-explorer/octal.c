/**
 * @file octal.c
 * @brief octal-explorer: computation of nim-sequences of octal and hexadecimal take-and-break games.
 *
 * This file is the computation: rules, all methods, periodicity, and the
 * library API (octal.h).  The command-line program and its output files
 * (PNG plots, index.html, summary.txt, values.csv) are in octal_output.c;
 * declarations shared by the two are in octal_internal.h.
 *
 * Code syntax: [d0].d1d2d3...  Each digit is hex (0-9, a-f).  Bit j of digit d_k
 * means: you may remove k beans from a heap and leave exactly j nonempty heaps
 * (j = 0..3; j = 3 needs a hexadecimal digit >= 8).  d0 (before the point) allows
 * splitting a heap without removing anything (e.g. "4." = split any heap).
 *
 * Algorithm: the default is a tight brute-force mex over all options
 * (O(n) per heap size for two-heap moves).  `--sparse` enables an experimental
 * rare/common variant: values are split by the parity of popcount(v & mask);
 * a rare XOR needs exactly one rare heap, so rare reachable values come from
 * the few rare heaps, and the pair scan stops once the smallest missing value
 * is rare.  Proving that a *common* value is unreachable still needs a full
 * scan, so on .6 and .007 it is not faster.  `--check` compares both methods.
 * `--blocked` (with `-j THREADS`) computes a block of heap sizes at once: the
 * pairs whose heaps both lie below the block are scanned in parallel with
 * OpenMP, only the few pairs inside the block are done sequentially (see
 * extend_blocked()).  Same values as brute force; needs -fopenmp for threads.
 * `--gpu` does the parallel scan of each block on a GPU with a Vulkan compute
 * shader (octal_gpu.comp, see extend_device()); needs -DOCTAL_VULKAN -lvulkan.
 * `--rare` is Grossman's rare-value algorithm (Games of No Chance 5, 2017; see
 * extend_rare()): exact, nearly independent of n per value when few heap sizes
 * have rare values.  With -j 1 one core; otherwise speculation: step 1 on a
 * small team of threads with baton passing, the full check of each chunk in
 * parallel on the others.
 * `--rocm` does the same with a HIP kernel (octal_rocm.hip, compiled with
 * hipcc and linked in); needs -DOCTAL_ROCM and the object file.
 *
 * Caching: there is no memo/hash table of positions.  Values are computed
 * bottom-up (dynamic programming) into one array G; G(n) is read off the
 * stored G(a), G(b) of smaller heaps, so every G(k) is computed once and
 * reused for all larger n.  The mex uses a stamp table (see Scratch) that is
 * never cleared between heap sizes.  Nothing is kept between library calls:
 * octal_extend() continues from the G[0..from) passed in by the caller.
 *
 * The library used by the Python package (functions octal_rule_name,
 * octal_extend, octal_period, ...; see octal.h) is this file alone:
 * cc -shared -fPIC octal.c.  The command-line program is octal.c + octal_output.c.
 *
 * Sections: rules · Grundy computation · periodicity · library API.
 */
#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>
#ifdef _OPENMP
#include <omp.h>
#endif

#include "octal.h"
#include "octal_internal.h"

/** Names of the methods, indexed by Method. */
const char *const method_name[] = {"brute", "sparse", "blocked", "gpu", "rocm", "rare"};

/** Print "octal: msg" to stderr and exit with status 1. */
void die(const char *msg) {
    fprintf(stderr, "octal: %s\n", msg);
    exit(1);
}

/** malloc() that never returns NULL (dies instead); n = 0 allocates 1 byte. */
void *xmalloc(size_t n) {
    void *p = malloc(n ? n : 1);
    if (!p) die("out of memory");
    return p;
}

/** calloc() that never returns NULL (dies instead). */
void *xcalloc(size_t n, size_t s) {
    void *p = calloc(n ? n : 1, s ? s : 1);
    if (!p) die("out of memory");
    return p;
}

/** Monotonic clock in seconds, for timing. */
double now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

/** Progress report: called with the number of heap sizes now final (G[0..n) correct). */
typedef void (*ProgressFn)(long n, void *ctx);

/* ------------------------------------------------------------------ rules */

/** Value of a hexadecimal digit character, or -1 if @p c is not one. */
static int hexval(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    c = tolower(c);
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

/**
 * @brief Parse a game code into a Rule.
 *
 * Accepts "grundy", "couples" (case-insensitive) and codes "[d0].d1d2..." where
 * each digit is hexadecimal.  A leading "0" before the point is allowed
 * ("0.07" == ".07").  Bits 0 and 1 of d0 are cleared, since they would describe
 * non-moves.
 *
 * @param s  code string
 * @param R  output (fully overwritten)
 * @return 0 on success, -1 if @p s is not a valid code or has no nonzero digit
 */
int parse_rule(const char *s, Rule *R) {
    memset(R, 0, sizeof *R);
    snprintf(R->code, sizeof R->code, "%s", s);
    if (!strcasecmp(s, "grundy")) {
        R->d[0] = 4; R->special = SP_GRUNDY;
        snprintf(R->name, sizeof R->name, "Grundy's game (split a heap into two unequal heaps)");
        return 0;
    }
    if (!strcasecmp(s, "couples")) {
        R->d[0] = 4; R->special = SP_COUPLES;
        snprintf(R->name, sizeof R->name, "Couples-are-forever (split any heap except a heap of 2)");
        return 0;
    }
    const char *dot = strchr(s, '.');
    if (!dot) return -1;
    if (dot - s > 1) {
        /* allow a leading "0" or one digit d0 */
        if (dot - s == 2 && s[0] == '0') s++;
        else return -1;
    }
    if (dot - s == 1) {
        int v = hexval(s[0]);
        if (v < 0) return -1;
        R->d[0] = (unsigned)v & ~3u; /* bits 0/1 of d0 are meaningless */
    }
    int k = 1;
    for (const char *p = dot + 1; *p; p++, k++) {
        int v = hexval(*p);
        if (v < 0 || k > MAXDIG) return -1;
        R->d[k] = (unsigned)v;
    }
    R->t = 0;
    for (int i = 0; i <= MAXDIG; i++) {
        if (R->d[i]) R->t = i;
        if (R->d[i] & 8) R->hex = 1;
    }
    if (R->t == 0 && R->d[0] == 0) return -1;
    snprintf(R->name, sizeof R->name, "%s game %s", R->hex ? "hexadecimal" : "octal", R->code);
    return 0;
}

/* ------------------------------------------------------ Grundy computation */

/**
 * @brief Scratch memory of one computation: the stamp table for the mex.
 *
 * seen[v] == n means "v is the value of an option of heap size n".  Using n as
 * the stamp avoids clearing the table for every n; new entries are 0xFFFFFFFF,
 * which never equals a valid n.  Every computation owns its own Scratch, so
 * computations in different threads never share memory (the library is
 * reentrant and thread-safe).
 */
typedef struct {
    uint32_t *seen;      /**< stamp table, indexed by nim-value */
    size_t cap;          /**< number of entries allocated */
} Scratch;

/**
 * @brief Grow @p S to at least @p need entries (a power of two); new entries are unmarked.
 * @return 0, or -1 if memory is exhausted (S is then unchanged and still valid)
 */
static int scratch_reserve(Scratch *S, size_t need) {
    if (need <= S->cap) return 0;
    size_t c = S->cap ? S->cap : 1024;
    while (c < need) c *= 2;
    uint32_t *p = realloc(S->seen, c * sizeof *p);
    if (!p) return -1;
    memset(p + S->cap, 0xff, (c - S->cap) * sizeof *p);
    S->seen = p;
    S->cap = c;
    return 0;
}

/** Release the memory of @p S and reset it to empty. */
static void scratch_free(Scratch *S) {
    free(S->seen);
    S->seen = NULL;
    S->cap = 0;
}

/** Sparse mode: value @p v is "rare" iff popcount(v & mask) is odd.
 *  An XOR of two values is rare iff exactly one of them is rare. */
static inline int is_rare(unsigned v, unsigned mask) {
    return __builtin_parity(v & mask);
}

/**
 * @brief Minimal excludant from @p start: the smallest value v >= start that is
 *        not marked with stamp @p st (S->seen[v] != st).
 *
 * The caller must have marked all options of the current heap with @p st
 * beforehand.  The result is bounded by the size of the table (see
 * scratch_reserve()).
 */
static inline unsigned mex_from(const Scratch *S, unsigned start, uint32_t st) {
    unsigned v = start;
    while (S->seen[v] == st) v++;
    return v;
}

/** Minimal excludant: the smallest value not marked with stamp @p st in @p S. */
static inline unsigned mex(const Scratch *S, uint32_t st) {
    return mex_from(S, 0, st);
}

/**
 * @brief Mark the values of all 0-, 1- and 3-heap options of heap size @p n.
 *
 * Two-heap options are marked by the callers (brute force or sparse).
 * Three-heap splits a <= b <= c are enumerated directly: O(n^2) per call.
 *
 * @param S   scratch (stamp table)
 * @param R   rule
 * @param G   nim-values of all heap sizes < n
 * @param n   heap size
 * @param st  stamp to write into S->seen (normally n)
 */
static void mark_simple(Scratch *S, const Rule *R, const uint16_t *G, long n, uint32_t st) {
    uint32_t *seen = S->seen;
    int tmax = R->t < n ? R->t : (int)n;
    for (int k = 0; k <= tmax; k++) {
        unsigned d = R->d[k];
        if (!d) continue;
        long m = n - k;
        if ((d & 1) && m == 0) seen[0] = st;
        if ((d & 2) && k > 0 && m >= 1) seen[G[m]] = st;
        if ((d & 8) && m >= 3)
            for (long a = 1; 3 * a <= m; a++)
                for (long b = a; a + 2 * b <= m; b++)
                    seen[G[a] ^ G[b] ^ G[m - a - b]] = st;
    }
}

/** Is splitting heap @p n into heaps @p a and @p b allowed by the named rule?
 *  Grundy's game forbids a == b; Couples forbids splitting a heap of 2. */
static inline int pair_allowed(const Rule *R, long n, long a, long b) {
    if (R->special == SP_GRUNDY && a == b) return 0;
    if (R->special == SP_COUPLES && n == 2) return 0;
    return 1;
}

/**
 * @brief Nim-value of heap size @p n by brute force.
 *
 * Marks every option (all two-heap splits a + b = n - k with 1 <= a <= b, plus
 * mark_simple()) and returns their mex.  O(n) for octal codes.
 *
 * @param S      scratch; must have room for every XOR of values in G
 * @param R      rule
 * @param G      nim-values of all heap sizes < n
 * @param n      heap size
 * @param pairs  incremented by the number of two-heap splits examined
 * @return G(n)
 */
static unsigned grundy_brute(Scratch *S, const Rule *R, const uint16_t *G, long n, long long *pairs) {
    uint32_t *seen = S->seen;
    uint32_t st = (uint32_t)n;
    mark_simple(S, R, G, n, st);
    int tmax = R->t < n ? R->t : (int)n;
    for (int k = 0; k <= tmax; k++) {
        if (!(R->d[k] & 4)) continue;
        long m = n - k;
        if (m < 2) continue;
        for (long a = 1; 2 * a <= m; a++) {
            long b = m - a;
            if (!pair_allowed(R, n, a, b)) continue;
            seen[G[a] ^ G[b]] = st;
        }
        *pairs += m / 2;
    }
    return mex(S, st);
}

/**
 * @brief Nim-value of heap size @p n with the rare/common (sparse) method.
 *
 * 1. Marks all options from pairs containing a rare heap (the only source of
 *    rare XORs), plus mark_simple().
 * 2. Scans all pairs, tracking the current mex `lo`; stops as soon as `lo` is
 *    rare, because every rare reachable value is already marked.
 *
 * Gives the same result as grundy_brute() (checked by `--check`), but is not
 * faster on games whose nim-values are mostly common.
 *
 * @param S      scratch; must have room for every XOR of values in G
 * @param R      rule
 * @param G      nim-values of all heap sizes < n
 * @param n      heap size
 * @param mask   rare/common mask (see is_rare())
 * @param rare   ascending list of rare heap sizes < n
 * @param nrare  length of @p rare
 * @param pairs  incremented by the number of pairs scanned in step 2
 * @return G(n)
 */
static unsigned grundy_sparse(Scratch *S, const Rule *R, const uint16_t *G, long n, unsigned mask,
                              const long *rare, long nrare, long long *pairs) {
    uint32_t *seen = S->seen;
    uint32_t st = (uint32_t)n;
    mark_simple(S, R, G, n, st);
    int tmax = R->t < n ? R->t : (int)n;
    /* every rare reachable value involves exactly one rare heap */
    for (int k = 0; k <= tmax; k++) {
        if (!(R->d[k] & 4)) continue;
        long m = n - k;
        if (m < 2) continue;
        for (long i = 0; i < nrare && rare[i] < m; i++) {
            long a = rare[i], b = m - a;
            if (!pair_allowed(R, n, a, b)) continue;
            seen[G[a] ^ G[b]] = st;
        }
    }
    unsigned lo = mex(S, st);
    if (is_rare(lo, mask)) return lo;
    /* confirm common values by scanning pairs; stop once the gap is rare */
    for (int k = 0; k <= tmax; k++) {
        if (!(R->d[k] & 4)) continue;
        long m = n - k;
        if (m < 2) continue;
        for (long a = 1; 2 * a <= m; a++) {
            long b = m - a;
            if (!pair_allowed(R, n, a, b)) continue;
            unsigned x = G[a] ^ G[b];
            seen[x] = st;
            (*pairs)++;
            if (x == lo) {
                lo = mex_from(S, lo, st);
                if (is_rare(lo, mask)) return lo;
            }
        }
    }
    return lo;
}

/**
 * @brief Choose the sparse-mode mask that makes the fewest heap sizes rare.
 *
 * Tries every mask 1 .. vcap-1 against the value histogram of G[1..n).
 *
 * @param G           nim-values
 * @param n           number of values to consider
 * @param vcap        power of two above the largest value
 * @param rare_count  out: number of rare heap sizes for the chosen mask
 * @return the chosen mask
 */
static unsigned choose_mask(const uint16_t *G, long n, unsigned vcap, long *rare_count) {
    long *cnt = xcalloc(vcap, sizeof *cnt);
    for (long i = 1; i < n; i++) cnt[G[i]]++;
    unsigned best = 0;
    long bestc = -1;
    for (unsigned m = 1; m < vcap; m++) {
        long c = 0;
        for (unsigned v = 1; v < vcap; v++)
            if (cnt[v] && is_rare(v, m)) c += cnt[v];
        if (bestc < 0 || c < bestc) { bestc = c; best = m; }
    }
    free(cnt);
    *rare_count = bestc < 0 ? 0 : bestc;
    return best;
}

/* ------------------------------------------------- blocked (multi-threaded) */

/** Heap sizes below this use grundy_brute(); blocks of smaller heaps are too small to pay off. */
#define BLOCK_MIN_N 4096
/** Largest block (number of heap sizes computed together). */
#define BLOCK_MAX 65536
/** Memory budget for the per-block option marks (bytes). */
#define BLOCK_MARK_BYTES (64L << 20)
/** Tile width in a for the parallel pair scan, so G[a] and G[m-a] stay in cache. */
#define BLOCK_TILE 4096

/** Number of threads for the blocked method: @p threads, or all (OpenMP default) if <= 0. */
static int block_threads(int threads) {
#ifdef _OPENMP
    return threads > 0 ? threads : omp_get_max_threads();
#else
    (void)threads;
    return 1;
#endif
}

/**
 * @brief Parallel phase of a block: two-heap options with both heaps below @p n0.
 *
 * For every heap n in [n0, n0+B) and every pair a + b = n - k with
 * a < b < n0, sets marks[(n-n0)*vcap + (G[a]^G[b])] = 1.  Those values are all
 * known, so the heaps of the block are independent: each thread owns a range
 * of heaps (and its rows of @p marks), and walks a in tiles of BLOCK_TILE so
 * the G[a] and G[m-a] it reads stay in cache across its heaps.
 *
 * @param R         rule
 * @param G         nim-values; only G[0..n0) is read
 * @param n0        first heap size of the block
 * @param B         number of heap sizes in the block
 * @param vcap      power of two above every value in G[0..n0) (row length)
 * @param marks     B * vcap bytes, zeroed by the caller
 * @param nthreads  number of OpenMP threads
 */
static void block_scan(const Rule *R, const uint16_t *G, long n0, long B, unsigned vcap,
                       uint8_t *marks, int nthreads) {
    int ks[MAXDIG + 1], nk = 0;
    for (int k = 0; k <= R->t; k++)
        if (R->d[k] & 4) ks[nk++] = k;
    if (!nk) return;
    long amax = (n0 + B - 2) / 2;
#pragma omp parallel num_threads(nthreads)
    {
        int tid = 0, nth = 1;
#ifdef _OPENMP
        tid = omp_get_thread_num();
        nth = omp_get_num_threads();
#endif
        long h0 = B * tid / nth, h1 = B * (tid + 1) / nth;
        for (long A = 1; A <= amax; A += BLOCK_TILE) {
            long Aend = A + BLOCK_TILE - 1;
            for (long h = h0; h < h1; h++) {
                uint8_t *mk = marks + (size_t)h * vcap;
                for (int i = 0; i < nk; i++) {
                    long m = n0 + h - ks[i];
                    long lo = m - n0 + 1, hi = (m - 1) / 2;   /* a < b < n0 */
                    if (lo < A) lo = A;
                    if (hi > Aend) hi = Aend;
                    const uint16_t *Gm = G + m;
                    for (long a = lo; a <= hi; a++) mk[G[a] ^ Gm[-a]] = 1;
                }
            }
        }
    }
}

/**
 * @brief Record G(n) = @p g and grow the scratch when a new maximum appears.
 * @return 0, -2 (value > 65535) or -3 (out of memory)
 */
static int store_value(Scratch *S, uint16_t *G, long n, unsigned g, unsigned *maxG, unsigned *vcap) {
    if (g > 65535) return -2;
    G[n] = (uint16_t)g;
    if (g > *maxG) {
        *maxG = g;
        while (*vcap <= *maxG) *vcap *= 2;
        if (scratch_reserve(S, 2 * (size_t)*vcap + 2)) return -3;
    }
    return 0;
}

/**
 * @brief Sequential phase of a block: finish heaps n0 .. n0+B-1 in order.
 *
 * Adds the options block_scan() could not see, because they involve a heap
 * of the block itself: pairs with b >= n0 (then a = m - b < B, so at most B
 * per heap), the pair a == b, and all 0-, 1- and 3-heap options
 * (mark_simple()).  G(n) is the mex over marks row n and the stamp table.
 *
 * @param S       scratch (stamp table); grown when new maxima appear
 * @param R       rule
 * @param G       nim-values; G[0..n0) valid, G[n0..n0+B) is written
 * @param n0      first heap size of the block
 * @param B       number of heap sizes in the block
 * @param vcap0   value capacity when the block was scanned (row length of @p marks)
 * @param marks   B rows of option marks for values < vcap0: one byte per value
 *                (block_scan()), or with @p bitset one bit per value in
 *                uint32 words, ceil(vcap0/32) words per row (the GPU scan)
 * @param bitset  layout of @p marks, see above
 * @param maxG    in/out: largest value so far
 * @param vcap    in/out: power of two above @p maxG
 * @param pairs   incremented by the number of two-heap splits examined
 * @return 0, -2 or -3 (see store_value()); G is valid up to where it stopped
 */
static int block_finish(Scratch *S, const Rule *R, uint16_t *G, long n0, long B, unsigned vcap0,
                        const void *marks, int bitset, unsigned *maxG, unsigned *vcap,
                        long long *pairs) {
    for (long h = 0; h < B; h++) {
        long n = n0 + h;
        uint32_t st = (uint32_t)n;
        mark_simple(S, R, G, n, st);
        uint32_t *seen = S->seen;
        for (int k = 0; k <= R->t; k++) {
            if (!(R->d[k] & 4)) continue;
            long m = n - k;
            if (m < 2) continue;
            long hi = (m - 1) / 2;
            if (hi > m - n0) hi = m - n0;               /* b = m - a >= n0 */
            for (long a = 1; a <= hi; a++) seen[G[a] ^ G[m - a]] = st;
            if (!(m & 1) && pair_allowed(R, n, m / 2, m / 2)) seen[0] = st;   /* a == b */
            *pairs += m / 2;
        }
        unsigned v = 0;
        if (bitset) {
            size_t nw = (vcap0 + 31) / 32;
            const uint32_t *mk = (const uint32_t *)marks + (size_t)h * nw;
            while ((v < vcap0 && (mk[v >> 5] >> (v & 31) & 1)) || seen[v] == st) v++;
        } else {
            const uint8_t *mk = (const uint8_t *)marks + (size_t)h * vcap0;
            while ((v < vcap0 && mk[v]) || seen[v] == st) v++;
        }
        int rc = store_value(S, G, n, v, maxG, vcap);
        if (rc) return rc;
    }
    return 0;
}

/**
 * @brief Compute G[from..to) with the blocked, multi-threaded method.
 *
 * Heap sizes below BLOCK_MIN_N use grundy_brute().  Above, the heaps are
 * processed in blocks [n0, n0+B), B ~ n0/32 (at most BLOCK_MAX, and B * vcap
 * bytes of marks at most BLOCK_MARK_BYTES):
 *
 * 1. block_scan(): all pairs a < b < n0, in parallel (almost all the work);
 * 2. block_finish(): the remaining O(B) options per heap, in order of n.
 *
 * The sequential part is about B/n0 ~ 1/32 of the pair work, so speed-up is
 * close to the number of cores for large n.  Hexadecimal three-heap options
 * stay sequential (in mark_simple()), so hex codes are not faster.  Gives the
 * same values as grundy_brute() (`--blocked --check`).
 *
 * @param R        rule
 * @param G        array of at least @p to entries; G[0..from) valid
 * @param from     first heap size to compute
 * @param to       one past the last heap size
 * @param threads  number of threads, <= 0 for all
 * @param pairs    incremented by the number of two-heap splits examined
 * @return 0, -2 (value > 65535) or -3 (out of memory)
 */
static int extend_blocked(const Rule *R, uint16_t *G, long from, long to, int threads,
                          long long *pairs) {
    int nt = block_threads(threads);
    unsigned maxG = 0, vcap = 2;
    for (long i = 0; i < from; i++)
        if (G[i] > maxG) maxG = G[i];
    while (vcap <= maxG) vcap *= 2;
    Scratch S = {0};
    if (scratch_reserve(&S, 2 * (size_t)vcap + 2)) return -3;
    uint8_t *marks = NULL;
    size_t mcap = 0;
    int rc = 0;
    long n = from;
    while (n < to && !rc) {
        if (n < BLOCK_MIN_N) {
            rc = store_value(&S, G, n, grundy_brute(&S, R, G, n, pairs), &maxG, &vcap);
            n++;
            continue;
        }
        long B = n / 32;
        if (B > BLOCK_MAX) B = BLOCK_MAX;
        if (B > BLOCK_MARK_BYTES / (long)vcap) B = BLOCK_MARK_BYTES / (long)vcap;
        if (B < 1) B = 1;
        if (B > to - n) B = to - n;
        size_t need = (size_t)B * vcap;
        if (need > mcap) {
            uint8_t *p = realloc(marks, need);
            if (!p) { rc = -3; break; }
            marks = p;
            mcap = need;
        }
        memset(marks, 0, need);
        unsigned vcap0 = vcap;
        block_scan(R, G, n, B, vcap0, marks, nt);
        rc = block_finish(&S, R, G, n, B, vcap0, marks, 0, &maxG, &vcap, pairs);
        n += B;
    }
    free(marks);
    scratch_free(&S);
    return rc;
}

/* ------------------------------------- rare-value algorithm (Grossman 2017) */

/**
 * Rare-value algorithm: value @p v is *rare* iff popcount(v & mask) is EVEN.
 *
 * This is the convention of Winning Ways and of Grossman, "Searching for
 * periodicity in Officers" (Games of No Chance 5, 2017).  The rare values form
 * a group under XOR (0 is always rare) and the common values its other coset:
 * rare^rare = rare, rare^common = common, common^common = rare.  (The older
 * sparse mode above uses the opposite convention, see grundy_sparse().)
 */
static inline int rv_rare(unsigned v, unsigned mask) {
    return !__builtin_parity(v & mask);
}

/**
 * @brief Choose the rare-value mask: the one that makes the fewest heap sizes rare.
 *
 * Tries every mask 1 .. vcap-1 against the value histogram of G[1..n).
 *
 * @param G           nim-values
 * @param n           number of values to consider
 * @param vcap        power of two above the largest value
 * @param rare_count  out: number of rare heap sizes in 1 .. n-1 for the chosen mask
 * @return the chosen mask
 */
static unsigned rv_choose_mask(const uint16_t *G, long n, unsigned vcap, long *rare_count) {
    long *cnt = xcalloc(vcap, sizeof *cnt);
    for (long i = 1; i < n; i++) cnt[G[i]]++;
    unsigned best = 1;
    long bestc = -1;
    for (unsigned m = 1; m < vcap; m++) {
        long c = 0;
        for (unsigned v = 0; v < vcap; v++)
            if (cnt[v] && rv_rare(v, m)) c += cnt[v];
        if (bestc < 0 || c < bestc) { bestc = c; best = m; }
    }
    free(cnt);
    *rare_count = bestc < 0 ? 0 : bestc;
    return best;
}

/*
 * Rare-value algorithm for one heap size (Grossman 2017, Figure 2, generalised
 * to any octal code).  rare_step1() does step 1, grundy_rare() both steps.
 *
 * 1. Every *common* reachable value comes from a pair with exactly one rare
 *    heap (rare ^ common = common; the other two products are rare).  Looping
 *    over the rare heap sizes therefore marks ALL reachable common values; the
 *    0-, 1- and 3-heap options are marked in full by mark_simple().  The
 *    smallest unmarked common value, `cval`, is certainly not reachable.
 * 2. So G(n) = cval unless some rare value below cval is unreachable.  Scan all
 *    pairs, counting down the unmarked rare values below cval, and stop as soon
 *    as none is left.  Almost every pair is common ^ common, which is rare, so
 *    this usually stops after a few thousand pairs.  If the scan ends with rare
 *    values still unmarked, G(n) is the smallest of them: a new rare value.
 *
 * Both steps are exact for every mask; a mask with few rare heap sizes only
 * makes step 1 short.
 */

/**
 * @brief Step 1 of the rare-value algorithm: mark all common reachable values, return `cval`.
 *
 * Leaves the marks in @p S (stamp n) for step 2.  On its own this is the
 * *speculative* value of Grossman section 3.1: G(n) under the assumption that
 * no rare value below cval is unreachable (see rare_spec_run()).  The result
 * is always a common value.
 *
 * @param S      scratch; must have room for 2 * vcap + 2 values
 * @param R      rule
 * @param G      nim-values of all heap sizes < n
 * @param n      heap size
 * @param mask   rare-value mask, below the current vcap
 * @param rare   ascending list of the heap sizes 1 .. n-1 with a rare value
 * @param nrare  length of @p rare
 * @param st     statistics: scan1 is incremented
 * @return the smallest common value that is not reachable from heap size @p n
 */
static unsigned rare_step1(Scratch *S, const Rule *R, const uint16_t *G, long n, unsigned mask,
                           const long *rare, long nrare, RareStats *st) {
    uint32_t *seen = S->seen;
    uint32_t stamp = (uint32_t)n;
    mark_simple(S, R, G, n, stamp);
    int tmax = R->t < n ? R->t : (int)n;
    /* every pair (a, m-a) with a rare heap a; the partner may be rare too (that
     * marks a rare value, which is harmless) */
    for (int k = 0; k <= tmax; k++) {
        if (!(R->d[k] & 4)) continue;
        long m = n - k;
        if (m < 2) continue;
        for (long i = 0; i < nrare && rare[i] < m; i++) {
            long a = rare[i], b = m - a;
            if (!pair_allowed(R, n, a, b)) continue;
            seen[G[a] ^ G[b]] = stamp;
        }
        st->scan1 += nrare;
    }
    /* smallest common value not marked; it exists below 2 * vcap, because no
     * option value reaches vcap and [vcap, 2 vcap) contains common values */
    unsigned cval = 0;
    while (seen[cval] == stamp || rv_rare(cval, mask)) cval++;
    return cval;
}

/**
 * @brief Nim-value of heap size @p n with the rare-value algorithm (exact).
 *
 * rare_step1(), then step 2: look for a witness pair for every rare value below
 * `cval` and stop when all are found (see the comment above rare_step1()).
 *
 * @param S      scratch; must have room for 2 * vcap + 2 values
 * @param R      rule
 * @param G      nim-values of all heap sizes < n
 * @param n      heap size
 * @param mask   rare-value mask, below the current vcap
 * @param rare   ascending list of the heap sizes 1 .. n-1 with a rare value
 * @param nrare  length of @p rare
 * @param st     statistics: scan1, scan2 and full are incremented
 * @return G(n)
 */
static unsigned rare_step2(Scratch *S, const Rule *R, const uint16_t *G, long n, unsigned mask, unsigned cval,
                           RareStats *st);

/**
 * Step 2 with vectors (GCC vector extensions; AVX2/AVX-512 with -march=native).
 *
 * Step 2 spends most of its pairs on the last few missing rare values: for .6,
 * fewer than 8 are left after about 840 of 2750 pairs per heap size (grundy:
 * 650 of 3140).  Once at most RARE_TAIL values are missing, rare_step2() takes
 * 16 pairs at once and compares their XORs with each missing value; only a block
 * with a hit is gone through pair by pair.  Same marks, same result, same pair
 * counts as the scalar loop.
 */
#if defined(__GNUC__) && !defined(__clang__)
#define RARE_SIMD 1
typedef uint16_t v16u16 __attribute__((vector_size(32)));
typedef int16_t v16s16 __attribute__((vector_size(32)));
typedef uint64_t v4u64 __attribute__((vector_size(32)));
#endif
/** Step 2 switches to vectors once at most this many rare values are missing. */
#define RARE_TAIL 8

static unsigned grundy_rare(Scratch *S, const Rule *R, const uint16_t *G, long n, unsigned mask,
                            const long *rare, long nrare, RareStats *st) {
    unsigned cval = rare_step1(S, R, G, n, mask, rare, nrare, st);
    return rare_step2(S, R, G, n, mask, cval, st);
}

/**
 * @brief Step 2 of the rare-value algorithm: G(n), given `cval` and the marks of step 1.
 *
 * Needs, stamped with n in @p S, every common value below @p cval and all rare
 * values that step 1 or the 0-, 1- and 3-heap options reach (rare_step1() leaves
 * exactly that; grundy_rare_bytes() sets it up from the byte step 1).
 *
 * @param S      scratch with the marks (stamp n)
 * @param R      rule
 * @param G      nim-values of all heap sizes < n
 * @param n      heap size
 * @param mask   rare-value mask
 * @param cval   smallest common value not reachable from n
 * @param st     statistics: scan2 and full are incremented
 * @return G(n)
 */
static unsigned rare_step2(Scratch *S, const Rule *R, const uint16_t *G, long n, unsigned mask, unsigned cval,
                           RareStats *st) {
    uint32_t *seen = S->seen;
    uint32_t stamp = (uint32_t)n;
    int tmax = R->t < n ? R->t : (int)n;
    /* every common value below cval is marked (step 1 is complete for common
     * values), so G(n) < cval can only be an unmarked rare value */
    long need = 0;                      /* unmarked rare values below cval */
    for (unsigned v = 0; v < cval; v++)
        if (rv_rare(v, mask) && seen[v] != stamp) need++;
    if (!need) return cval;
    /* 2. find a witness pair for every rare value below cval */
#ifdef RARE_SIMD
    uint16_t want[RARE_TAIL];           /* the unmarked rare values below cval ... */
    v16u16 wv[RARE_TAIL];               /* ... broadcast to vectors */
    int nw = -1;                        /* entries of want; -1: not built yet */
#endif
    for (int k = 0; k <= tmax; k++) {
        if (!(R->d[k] & 4)) continue;
        long m = n - k;
        for (long a = 1; 2 * a <= m; a++) {
#ifdef RARE_SIMD
            /* the tail: few values left, 16 pairs (a + i, m - a - i) at once;
             * all have a + i < m - a - i, so pair_allowed() holds for n > 2 */
            if (need <= RARE_TAIL && 2 * a + 32 <= m && n > 2) {
                if (nw < 0) {
                    nw = 0;
                    for (unsigned v = 0; v < cval; v++)
                        if (rv_rare(v, mask) && seen[v] != stamp) {
                            want[nw] = (uint16_t)v;
                            wv[nw] = (v16u16){0} + want[nw];
                            nw++;
                        }
                }
                v16u16 ga, gb;
                memcpy(&ga, G + a, sizeof ga);
                memcpy(&gb, G + m - a - 15, sizeof gb);
                gb = __builtin_shuffle(gb, (v16u16){15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0});
                v16u16 x = ga ^ gb;
                v16s16 hit = x == wv[0];
                for (int i = 1; i < nw; i++) hit |= x == wv[i];
                v4u64 h = (v4u64)hit;
                if (!(h[0] | h[1] | h[2] | h[3])) {
                    st->scan2 += 16;
                    a += 15;
                    continue;
                }
                /* a witness among them: go through the 16 pairs one by one */
                for (long e = a + 16; a < e; a++) {
                    unsigned y = G[a] ^ G[m - a];
                    st->scan2++;
                    if (y < cval && seen[y] != stamp) {
                        seen[y] = stamp;
                        if (--need == 0) return cval;
                        for (int i = 0; i < nw; i++)
                            if (want[i] == y) {
                                nw--;
                                want[i] = want[nw];
                                wv[i] = wv[nw];
                                break;
                            }
                    }
                }
                a--;
                continue;
            }
#endif
            long b = m - a;
            if (!pair_allowed(R, n, a, b)) continue;
            unsigned x = G[a] ^ G[b];
            st->scan2++;
            if (x < cval && seen[x] != stamp) {
                seen[x] = stamp;
                if (--need == 0) return cval;
#ifdef RARE_SIMD
                nw = -1;                /* rebuild want when the tail starts */
#endif
            }
        }
    }
    /* the full scan left rare values below cval unmarked: every option is now
     * marked, so the ordinary mex is G(n), the smallest of them */
    st->full++;
    return mex(S, stamp);
}

/** Fill @p rare (capacity *cap, grown) with the heap sizes 1 .. n-1 whose value is rare. */
static long rv_collect(const uint16_t *G, long n, unsigned mask, long **rare, long *cap) {
    long nr = 0;
    for (long i = 1; i < n; i++)
        if (rv_rare(G[i], mask)) {
            if (nr == *cap) {
                long c = *cap ? 2 * *cap : 1024;
                long *p = realloc(*rare, (size_t)c * sizeof *p);
                if (!p) return -1;
                *rare = p;
                *cap = c;
            }
            (*rare)[nr++] = i;
        }
    return nr;
}

/** Speculation starts once the rare-value method is active and n is at least this. */
#define RARE_SPEC_MIN 4096

/** Speculation chunk that starts at heap size @p x: n/8 heap sizes (4096 .. 2^18), ending at @p end at the latest. */
static long rare_chunk_end(long x, long end) {
    long len = x / 8;
    if (len < 4096) len = 4096;
    if (len > (1L << 18)) len = 1L << 18;
    return x + len < end ? x + len : end;
}

/**
 * @brief One byte per value for the speculation (Grossman 2017, section 3.2).
 *
 * Let j be a bit of the mask below 512.  Deleting bit j from a value,
 * byte(v) = (v & (2^j - 1)) | ((v >> (j + 1)) << j), is linear under XOR, and
 * on the common values below 512 it is one-to-one onto 0..255: for a common v
 * the parity condition fixes bit j.  So while all values are below 512, step 1
 * can work on bytes: `g8` holds byte(G(i)), the mark table has 256 entries, and
 * the speculation reads half as much memory (the last ~20,000 values of Officers
 * then fit in the 32 KB L1 cache instead of 40 KB).
 *
 * Step 1 marks G(a) ^ G(b) for a rare and b = m - a.  On bytes this is only
 * correct if the result is common, i.e. if b is never rare: then two different
 * marked values never share a byte.  That holds once n > 2 * (largest rare heap
 * size) + t, since then b > the largest rare heap size.  Before that, for codes
 * with 3-heap moves, and once a value reaches 512, rare_speculate() uses
 * rare_step1() instead.  So the speculative values are the same either way
 * (a debug build comparing both for every speculated heap size of .6, grundy,
 * couples and .77 up to 2*10^6 found no difference).
 *
 * Life cycle:
 * - rb_setup() at every mask (re)choice in extend_rare(): picks j (the highest
 *   mask bit below 512), the list of common values and their bytes, and resets
 *   g8 and the byte copy of the rare list; or switches the byte mode off.
 * - rb_catch_up() at the start of each rare_speculate(): fills g8 from G up
 *   to the first heap size of the chunk (values computed elsewhere, e.g. exact
 *   values after a failed check) and copies new rare heap sizes.
 * - rare_speculate() writes byte(g) for every speculative value it stores.
 * - After a failed check at heap size f, rare_spec_run() sets known = f, so the
 *   discarded speculative bytes from f on are refilled before they are read.
 * - A value of 512 or more switches the byte mode off for the rest of the
 *   computation (until the next rb_setup()).
 *
 * Effect (Ryzen 9 7900X, 24 threads): .6 to 10^7 in 10.4 s instead of 14.2 s;
 * the speculating thread computes 7.7 s instead of 13.0 s.
 *
 * Owned by the speculating thread (thread 0 of rare_spec_run()).
 */
typedef struct {
    int on;                   /**< usable: no 3-heap moves, all values < 512, mask has a bit below 512 */
    unsigned drop;            /**< the deleted bit j */
    uint8_t *g8;              /**< g8[i] = byte(G(i)) for i < known */
    long known, cap;          /**< valid entries / allocated entries of g8 */
    uint32_t *rpos;           /**< rare heap sizes ... */
    uint8_t *rval;            /**< ... and the bytes of their values */
    long nr, rcap;            /**< entries / capacity of rpos and rval */
    uint16_t common[256];     /**< the common values below 512, ascending ... */
    uint8_t cbyte[256];       /**< ... and their bytes */
    int ncommon;              /**< number of entries of common (256 if the mask has a bit below 512) */
    uint32_t mark[256];       /**< mark table, indexed by byte, stamped with n */
} RareBytes;

/** byte(v): delete bit @p j (see RareBytes). */
static inline unsigned rb_byte(unsigned v, unsigned j) {
    return (v & ((1u << j) - 1)) | ((v >> (j + 1)) << j);
}

/** Free @p B and reset it to zero. */
static void rb_free(RareBytes *B) {
    free(B->g8);
    free(B->rpos);
    free(B->rval);
    memset(B, 0, sizeof *B);
}

/**
 * @brief Prepare @p B for a new mask (at every mask (re)choice).
 *
 * Keeps the allocations; g8 and the rare list are refilled lazily by
 * rb_catch_up().  Turns the byte mode off where it does not apply.
 *
 * @param B     byte state
 * @param R     rule (byte mode needs no 3-heap moves)
 * @param mask  the new rare-value mask
 * @param maxG  largest value so far (byte mode needs < 512)
 * @param to    one past the largest heap size that will be computed
 * @return 0, or -3 if out of memory
 */
static int rb_setup(RareBytes *B, const Rule *R, unsigned mask, unsigned maxG, long to) {
    unsigned low = mask & 0x1ff;
    B->on = !R->hex && maxG < 512 && low != 0;
    B->known = 0;
    B->nr = 0;
    if (!B->on) return 0;
    B->drop = 31 - (unsigned)__builtin_clz(low);
    if (to > B->cap) {
        uint8_t *p = realloc(B->g8, (size_t)to);
        if (!p) return -3;
        B->g8 = p;
        B->cap = to;
    }
    B->ncommon = 0;
    for (unsigned v = 0; v < 512; v++)
        if (!rv_rare(v, mask)) {
            B->common[B->ncommon] = (uint16_t)v;
            B->cbyte[B->ncommon++] = (uint8_t)rb_byte(v, B->drop);
        }
    memset(B->mark, 0xff, sizeof B->mark);
    return 0;
}

/**
 * @brief Bring g8 up to heap size @p n and the byte copy of the rare list up to @p nrare.
 * @return 0, or -3 if out of memory (byte mode is then switched off)
 */
static int rb_catch_up(RareBytes *B, const uint16_t *G, long n, const long *rare, long nrare) {
    for (; B->known < n; B->known++) {
        if (G[B->known] >= 512) { B->on = 0; return 0; }
        B->g8[B->known] = (uint8_t)rb_byte(G[B->known], B->drop);
    }
    if (nrare > B->rcap) {
        long c = B->rcap ? 2 * B->rcap : 2048;
        while (c < nrare) c *= 2;
        uint32_t *p = realloc(B->rpos, (size_t)c * sizeof *p);
        uint8_t *q = p ? realloc(B->rval, (size_t)c) : NULL;
        if (p) B->rpos = p;
        if (q) B->rval = q;
        if (!p || !q) { B->on = 0; return -3; }
        B->rcap = c;
    }
    for (; B->nr < nrare; B->nr++) {
        B->rpos[B->nr] = (uint32_t)rare[B->nr];
        B->rval[B->nr] = B->g8[rare[B->nr]];
    }
    return 0;
}

/**
 * @brief rare_step1() on bytes (see RareBytes for when this is valid).
 *
 * Marks the common reachable values: 1-heap options with a common value, and
 * G(a) ^ G(m - a) for every rare a; rare results never occur here.  Rare
 * option values (rare 1-heap options, and the 0-heap option, whose value 0 is
 * always rare) cannot be G(n) under the speculative assumption, so they are
 * skipped.  The mark table is stamped with n, so it never needs clearing.  Returns the
 * smallest unmarked common value, or UINT_MAX if all 256 common values below
 * 512 are reachable (then the value does not fit and the caller uses
 * rare_step1()).  Needs g8[0..n) and the rare bytes (rb_catch_up()).
 *
 * @param B     byte state (read only)
 * @param mark  mark table of 256 entries, stamped with n (B->mark for the
 *              speculating thread; each checking thread has its own)
 * @param R     rule
 * @param G     nim-values (for the class of 1-heap options)
 * @param n     heap size
 * @param mask  rare-value mask
 * @param st    statistics: scan1 is incremented
 * @return G(n) under the speculative assumption, or UINT_MAX
 */
static unsigned rare_step1_bytes(const RareBytes *B, uint32_t *mark, const Rule *R, const uint16_t *G, long n,
                                 unsigned mask, RareStats *st) {
    uint32_t stamp = (uint32_t)n;
    const uint32_t *rpos = B->rpos;
    const uint8_t *rval = B->rval;
    long nr = B->nr;
    for (int k = 0; k <= R->t; k++) {
        unsigned d = R->d[k];
        if (!d) continue;
        long m = n - k;
        if ((d & 2) && k > 0 && !rv_rare(G[m], mask)) mark[B->g8[m]] = stamp;
        if (d & 4) {
            const uint8_t *gm = B->g8 + m;   /* gm[-a] = byte(G(m - a)) */
            for (long i = 0; i < nr; i++) mark[rval[i] ^ gm[-(long)rpos[i]]] = stamp;
            st->scan1 += nr;
        }
    }
    for (int i = 0; i < B->ncommon; i++)
        if (mark[B->cbyte[i]] != stamp) return B->common[i];
    return UINT_MAX;
}

/**
 * @brief grundy_rare() with step 1 on bytes: the check of the speculation (exact).
 *
 * Step 1 with rare_step1_bytes() on the thread's own @p mark table, which
 * gives `cval` as rare_step1() would (same conditions as for the speculation,
 * see RareBytes).  The byte marks are all common, so step 2 sees in @p S:
 * the common values below cval (all reachable, by step 1) and the 0-, 1- and
 * 3-heap options (mark_simple()).  Rare values reached by a pair of two rare
 * heaps are not marked here (rare_step1() marks them in passing); step 2 finds
 * them in its scan, so the result is the same, at the cost of a few more pairs.
 *
 * Independent of the baton passing: it recomputes every value from G, g8 and
 * the rare bytes alone.  The step 1 on bytes reads half the memory of
 * rare_step1(), and it skips pair_allowed() and the bound test per pair.
 *
 * @param S     scratch of the checking thread (2 * vcap + 2 entries)
 * @param B     byte state (read only; g8 valid below n, rare bytes complete)
 * @param mark  the checking thread's byte mark table (256 entries)
 * @param R     rule
 * @param G     nim-values of all heap sizes < n
 * @param n     heap size, at least the byte-mode limit (see rare_speculate())
 * @param mask  rare-value mask
 * @param st    statistics: scan1, scan2 and full are incremented
 * @return G(n), or UINT_MAX if it does not fit in a byte (the caller then uses grundy_rare())
 */
static unsigned grundy_rare_bytes(Scratch *S, const RareBytes *B, uint32_t *mark, const Rule *R,
                                  const uint16_t *G, long n, unsigned mask, RareStats *st) {
    unsigned cval = rare_step1_bytes(B, mark, R, G, n, mask, st);
    if (cval == UINT_MAX) return UINT_MAX;
    uint32_t stamp = (uint32_t)n;
    mark_simple(S, R, G, n, stamp);
    uint32_t *seen = S->seen;
    for (unsigned v = 0; v < cval; v++)
        if (!rv_rare(v, mask)) seen[v] = stamp;
    return rare_step2(S, R, G, n, mask, cval, st);
}

/**
 * @brief Speculate G[n0..e): step 1 only (rare_step1()), assuming no new rare value.
 *
 * Sequential, since each value depends on the previous ones.  Speculative
 * values are always common, so the rare list does not change.  Where it is
 * valid (see RareBytes), step 1 runs on bytes: rare_step1_bytes().
 *
 * @param S      scratch of the speculating thread (grown as values grow)
 * @param R      rule
 * @param G      nim-values; G[0..n0) are taken as given, G[n0..e) is written
 * @param n0     first heap size
 * @param e      one past the last heap size
 * @param mask   rare-value mask
 * @param rare   rare heap sizes < n0
 * @param nrare  length of @p rare
 * @param maxG   in/out: largest value so far
 * @param vcap   in/out: power of two above @p maxG
 * @param B      one-byte state (may be NULL)
 * @param st     statistics: scan1, speculated and bytes are incremented
 * @param rc     out: set to the store_value() error (-2 or -3) if one occurs
 * @return the end of the speculated range: @p e, or the heap size where store_value() failed
 */
static long rare_speculate(Scratch *S, const Rule *R, uint16_t *G, long n0, long e, unsigned mask,
                           const long *rare, long nrare, unsigned *maxG, unsigned *vcap, RareBytes *B,
                           RareStats *st, int *rc) {
    /* heap sizes from which no partner b = m - a of step 1 can be rare */
    long bytes_from = (nrare ? 2 * rare[nrare - 1] : 0) + R->t + 1;
    if (B && B->on && rb_catch_up(B, G, n0, rare, nrare)) B = NULL;
    long n = n0;
    for (; n < e; n++) {
        unsigned g = UINT_MAX;
        if (B && B->on && n >= bytes_from) {
            g = rare_step1_bytes(B, B->mark, R, G, n, mask, st);
            if (g != UINT_MAX) st->bytes++;
        }
        if (g == UINT_MAX) g = rare_step1(S, R, G, n, mask, rare, nrare, st);
        int r = store_value(S, G, n, g, maxG, vcap);
        if (r) { *rc = r; break; }
        if (B && B->on) {
            if (g >= 512) B->on = 0;
            else { B->g8[n] = (uint8_t)rb_byte(g, B->drop); B->known = n + 1; }
        }
    }
    st->speculated += n - n0;
    return n;
}

/**
 * @brief Baton passing (Grossman 2017, section 3.3): the speculation of one chunk by a team.
 *
 * A team of P threads speculates G[n0..e) in byte mode (RareBytes); thread j
 * takes the heap sizes n = n0 + j, n0 + j + P, ...  For each n it
 *
 * 1. *before the baton* marks every byte that does not depend on the last
 *    P - 1 values: step-1 pairs (a, m - a) with m - a <= n - P, i.e. rare
 *    a >= P - k for m = n - k, and 1-heap options n - k with k >= P.  These
 *    values are final: the thread's own previous heap size n - P had the baton,
 *    so everything below it was published, and it published n - P itself.
 *    Then it takes the smallest unmarked common value as the candidate
 *    (Grossman's *speculative mex*);
 * 2. waits for the baton: the shared counter `upto` (values below it are final)
 *    reaching n;
 * 3. *after the baton* marks the rest: the few rare a < P - k, and the 1-heap
 *    options with k < P.  Marks only grow, so the value is the candidate unless
 *    it got marked now; then the search continues from there;
 * 4. stores G(n) and g8[n] and passes the baton (upto = n + 1, release).
 *
 * Steps 1 and 3 together mark exactly what rare_step1_bytes() marks, so the
 * speculative values are the same as with one thread.  The critical path per
 * value is the baton hand-over plus step 3 (a handful of marks), instead of all
 * of step 1.
 *
 * If all 256 common bytes get marked (the value does not fit; rare_step1_bytes()
 * would return UINT_MAX), the thread holding the baton sets `stopat` and stops
 * without passing it; the threads waiting for later heap sizes see that and
 * stop too.  rare_speculate_chunk() then continues on one thread.
 *
 * Shared state of one team, set up by rare_speculate_chunk().
 */
typedef struct {
    long upto;         /**< atomic: G and g8 are final below this heap size (the baton) */
    long stopat;       /**< atomic: heap size where the team gave up (e if none) */
    long e;            /**< one past the last heap size of the chunk */
    int P;             /**< team size */
    int team;          /**< 1: baton passing; 0: thread 0 alone (rare_speculate()) */
    long done;         /**< rare_speculate_chunk(): end of the speculated range (set by thread 0) */
    int rc;            /**< rare_speculate_chunk(): store_value() error of thread 0 */
} Baton;

/** Spin-wait hint for the baton loop. */
static inline void cpu_relax(void) {
#if defined(__x86_64__) || defined(__i386__)
    __builtin_ia32_pause();
#endif
}

/**
 * @brief One thread (@p j of the team) of the baton-passing speculation; see Baton.
 *
 * Needs B->g8[0..n0) and the rare bytes (rb_catch_up()), n0 at least the
 * byte-mode limit of rare_speculate(), and no 3-heap moves (byte mode).
 *
 * @param B     byte state (g8 is written; mark is not used, each thread has its own)
 * @param R     rule
 * @param G     nim-values; G[n0..e) written
 * @param n0    first heap size of the chunk
 * @param mask  rare-value mask
 * @param T     shared team state
 * @param j     this thread's index in the team, 0 .. P-1
 * @param st    statistics: scan1 and bytes are added atomically
 */
static void rare_baton_worker(RareBytes *B, const Rule *R, uint16_t *G, long n0, unsigned mask, Baton *T,
                              int j, RareStats *st) {
    const int P = T->P;
    const uint32_t *rpos = B->rpos;
    const uint8_t *rval = B->rval;
    const uint8_t *g8 = B->g8;
    const long nr = B->nr;
    uint32_t mark[256];
    memset(mark, 0xff, sizeof mark);
    /* split[k]: first rare index with rpos >= P - k; below it the partner m - a
     * is one of the last P - 1 values (post-baton) */
    long split[MAXDIG + 1];
    long pairs_per_n = 0;
    for (int k = 0; k <= R->t; k++) {
        long i = 0;
        while (i < nr && (long)rpos[i] < P - k) i++;
        split[k] = i;
        if (R->d[k] & 4) pairs_per_n += nr;
    }
    long count = 0;
    for (long n = n0 + j; n < T->e; n += P) {
        uint32_t stamp = (uint32_t)n;
        /* 1. before the baton */
        for (int k = 0; k <= R->t; k++) {
            unsigned d = R->d[k];
            if (!d) continue;
            long m = n - k;
            if ((d & 2) && k >= P && !rv_rare(G[m], mask)) mark[g8[m]] = stamp;
            if (d & 4) {
                const uint8_t *gm = g8 + m;
                for (long i = split[k]; i < nr; i++) mark[rval[i] ^ gm[-(long)rpos[i]]] = stamp;
            }
        }
        int ci = 0;
        while (ci < B->ncommon && mark[B->cbyte[ci]] == stamp) ci++;
        /* 2. wait for the baton */
        while (__atomic_load_n(&T->upto, __ATOMIC_ACQUIRE) < n) {
            if (__atomic_load_n(&T->stopat, __ATOMIC_RELAXED) < n) goto out;
            cpu_relax();
        }
        /* 3. after the baton */
        for (int k = 0; k <= R->t; k++) {
            unsigned d = R->d[k];
            if (!d) continue;
            long m = n - k;
            if ((d & 2) && k > 0 && k < P && !rv_rare(G[m], mask)) mark[g8[m]] = stamp;
            if (d & 4) {
                const uint8_t *gm = g8 + m;
                for (long i = 0; i < split[k]; i++) mark[rval[i] ^ gm[-(long)rpos[i]]] = stamp;
            }
        }
        while (ci < B->ncommon && mark[B->cbyte[ci]] == stamp) ci++;
        if (ci == B->ncommon) {     /* does not fit in a byte: give up, keep the baton */
            __atomic_store_n(&T->stopat, n, __ATOMIC_RELAXED);
            break;
        }
        /* 4. store and pass the baton */
        G[n] = B->common[ci];
        B->g8[n] = B->cbyte[ci];
        __atomic_store_n(&T->upto, n + 1, __ATOMIC_RELEASE);
        count++;
    }
out:
    __atomic_add_fetch(&st->scan1, (long long)count * pairs_per_n, __ATOMIC_RELAXED);
    __atomic_add_fetch(&st->bytes, (long long)count, __ATOMIC_RELAXED);
}

/**
 * @brief Speculate G[n0..e): with a team (baton passing) where possible, else rare_speculate().
 *
 * Called by every thread @p j < T->P of a parallel region, after
 * rare_spec_prepare() set up @p T.  Thread 0 finishes the chunk: it waits for
 * the team, takes over sequentially where the team gave up, and updates maxG,
 * vcap and B->known.  Result in T->done (end of the speculated range) and T->rc
 * (as rare_speculate()).  The other team threads return as soon as they are done.
 */
static void rare_speculate_chunk(Scratch *S, const Rule *R, uint16_t *G, long n0, unsigned mask,
                                 const long *rare, long nrare, unsigned *maxG, unsigned *vcap, RareBytes *B,
                                 Baton *T, int j, RareStats *st) {
    if (!T->team) {
        if (j == 0) T->done = rare_speculate(S, R, G, n0, T->e, mask, rare, nrare, maxG, vcap, B, st, &T->rc);
        return;
    }
    rare_baton_worker(B, R, G, n0, mask, T, j, st);
    if (j) return;
    /* thread 0: wait until the team has finished or given up */
    long u;
    while ((u = __atomic_load_n(&T->upto, __ATOMIC_ACQUIRE)) < T->e &&
           __atomic_load_n(&T->stopat, __ATOMIC_RELAXED) >= T->e)
        cpu_relax();
    for (long i = n0; i < u; i++)
        if (G[i] > *maxG) *maxG = G[i];
    while (*vcap <= *maxG) *vcap *= 2;
    B->known = u;
    st->speculated += u - n0;
    if (scratch_reserve(S, 2 * (size_t)*vcap + 2)) { T->done = u; T->rc = -3; return; }
    T->done = u < T->e ? rare_speculate(S, R, G, u, T->e, mask, rare, nrare, maxG, vcap, B, st, &T->rc) : u;
}

/**
 * @brief Set up @p T for rare_speculate_chunk() on G[n0..e) with up to @p P threads.
 *
 * Baton passing needs at least 2 threads, the byte mode (RareBytes; caught up
 * to n0 here) and n0 at least the byte-mode limit of rare_speculate().
 */
static void rare_spec_prepare(Baton *T, const Rule *R, const uint16_t *G, long n0, long e, const long *rare,
                              long nrare, RareBytes *B, int P) {
    memset(T, 0, sizeof *T);
    T->e = e;
    T->upto = n0;
    T->stopat = e;
    T->done = n0;
    long bytes_from = (nrare ? 2 * rare[nrare - 1] : 0) + R->t + 1;
    T->team = P > 1 && B && B->on && n0 >= bytes_from && e - n0 >= P && !rb_catch_up(B, G, n0, rare, nrare) &&
              B->on;
    T->P = T->team ? P : 1;
}

/**
 * @brief Check part of the speculation: run by every checking thread.
 *
 * Recomputes G(n) for heap sizes of [n0, e) with the full grundy_rare(), taking
 * the speculative values before n as given, and records the smallest heap size
 * whose speculative value is wrong.  The heap sizes are independent, so the
 * threads take blocks of 64 from the shared @p cursor; a thread that joins late
 * (the speculating one) simply takes what is left.  Heap sizes above the
 * smallest wrong one found so far are skipped: only that one matters.
 * Reads G[0..e) only.
 *
 * @param R       rule
 * @param G       nim-values; G[0..e) speculative or correct
 * @param e       one past the last heap size to check (the first one is the
 *                initial value of @p cursor)
 * @param mask    rare-value mask
 * @param rare    rare heap sizes < n0
 * @param nrare   length of @p rare
 * @param need    size of the thread's own mark table (2 * vcap + 2)
 * @param B       byte state for grundy_rare_bytes(), or NULL (16-bit check only);
 *                must not change during the check
 * @param bytes_from  first heap size where @p B may be used (byte-mode limit)
 * @param cursor  shared: next heap size to hand out (atomic)
 * @param bad     shared: smallest wrong heap size found so far (e if none)
 * @param badval  shared: the correct value G(*bad)
 * @param oom     shared: set to 1 if the mark table cannot be allocated
 * @param st      shared statistics: scan1, scan2 and full are added (in a critical section)
 */
static void rare_check_worker(const Rule *R, const uint16_t *G, long e, unsigned mask,
                              const long *rare, long nrare, size_t need, const RareBytes *B, long bytes_from,
                              long *cursor, long *bad, unsigned *badval, int *oom, RareStats *st) {
    Scratch T = {0};
    RareStats loc = {0};
    uint32_t mark[256];
    memset(mark, 0xff, sizeof mark);
    if (scratch_reserve(&T, need)) {
#pragma omp atomic write
        *oom = 1;
    } else {
        for (;;) {
            long start, b;
#pragma omp atomic capture
            { start = *cursor; *cursor += 64; }
#pragma omp atomic read
            b = *bad;
            if (start >= e || start > b) break;
            long stop = start + 64 < e ? start + 64 : e;
            for (long n = start; n < stop && n <= b; n++) {
                unsigned g = B && n >= bytes_from ? grundy_rare_bytes(&T, B, mark, R, G, n, mask, &loc)
                                                  : UINT_MAX;
                if (g == UINT_MAX) g = grundy_rare(&T, R, G, n, mask, rare, nrare, &loc);
                if (g != G[n]) {
#pragma omp critical(octal_rare_bad)
                    if (n < *bad) { *bad = n; *badval = g; }
                    break;
                }
            }
        }
    }
#pragma omp critical(octal_rare_stats)
    {
        st->scan1 += loc.scan1;
        st->scan2 += loc.scan2;
        st->full += loc.full;
    }
    scratch_free(&T);
}

/** Append heap size @p n to the rare list.  0, or -3 if out of memory. */
static int rare_append(long **rare, long *nrare, long *cap, long n) {
    if (*nrare == *cap) {
        long c = *cap ? 2 * *cap : 1024;
        long *p = realloc(*rare, (size_t)c * sizeof *p);
        if (!p) return -3;
        *rare = p;
        *cap = c;
    }
    (*rare)[(*nrare)++] = n;
    return 0;
}

/**
 * @brief Speculation with overlapping check (Grossman 2017, section 3.1) on [n, end).
 *
 * The heap sizes are handled in chunks (rare_chunk_end()).  Chunk A is
 * speculated (rare_speculate()); then, in one parallel region,
 *
 * - the team, threads 0 .. P-1, speculates the next chunk B, assuming that A is
 *   correct (rare_speculate_chunk(): baton passing, see Baton), and then joins
 *   the check;
 * - all other threads check A (rare_check_worker()).
 *
 * This is safe: checking A reads only G below the end of A, and speculating B
 * writes only G from there on.
 *
 * - If every value of A agrees, A is correct (induction over n), and B becomes
 *   the chunk to check next, overlapped with the speculation of the one after.
 * - Otherwise let f be the first wrong heap size.  The values before f are
 *   correct, so the value the check computed for f is correct too: a new rare
 *   value.  It is stored and appended to the rare list; the rest of A and all of
 *   B are discarded, and speculation starts again at f + 1, without overlap.
 *
 * If storing a speculative value fails (store_value(): value > 65535, or out of
 * memory), the chunk ends there.  When the failing heap size comes up again as
 * the first one of a chunk, it is computed exactly with grundy_rare() instead,
 * so an error is only returned for a correct value.
 *
 * Performance (Ryzen 9 7900X, 24 threads, .6 to 10^7): 14 s, against 15 s
 * without the overlap and 61 s for the plain algorithm.  Spread over the
 * other threads the check is cheap, so the speculating thread is busy about
 * 85% of the time; the sequential speculation (about 1.3 us per value) is the
 * limit.  Idle checking threads must not spin on thread 0's physical core:
 * OMP_WAIT_POLICY=active costs about 25%, the default and "passive" are fine.
 * With baton passing (team of 4) and one call for the whole range, .6 to 10^7
 * took 5.0 s, limited by the check.  With the cheaper check (step 1 on bytes,
 * grundy_rare_bytes(); vectors in the tail of step 2, RARE_SIMD) and a team of
 * 6 it takes about 3.15 s.
 *
 * @param S      main scratch (used by thread 0)
 * @param R      rule
 * @param G      nim-values; G[0..n) correct, G[n..end) is written
 * @param n      first heap size
 * @param end    one past the last heap size
 * @param mask   rare-value mask
 * @param rare   in/out: rare heap sizes (new ones are appended)
 * @param nrare  in/out: length of *rare
 * @param cap    in/out: capacity of *rare
 * @param maxG   in/out: largest value so far
 * @param vcap   in/out: power of two above @p maxG
 * @param nt     number of threads (at least 2 to overlap anything; with one,
 *               thread 0 speculates B and then checks A alone)
 * @param P      size of the speculation team (at most nt - 1 while a check
 *               runs; 1: thread 0 alone)
 * @param B      one-byte state for the speculation (may be NULL)
 * @param st     statistics, updated
 * @param progress  called after each chunk that passed the check (may be NULL)
 * @param pctx      passed to @p progress
 * @return 0, -2 (value > 65535) or -3 (out of memory); G is correct below the
 *         heap size where it stopped
 */
static int rare_spec_run(Scratch *S, const Rule *R, uint16_t *G, long n, long end, unsigned mask,
                         long **rare, long *nrare, long *cap, unsigned *maxG, unsigned *vcap, int nt,
                         int P, RareBytes *B, RareStats *st, ProgressFn progress, void *pctx) {
    long a0 = n, a1 = n;            /* chunk A = [a0, a1): speculated, not yet checked */
    unsigned maxA = *maxG, vcapA = *vcap;   /* maxG and vcap over G[0..a0) */
    while (a0 < end) {
        int rc = 0;
        if (a1 == a0) {             /* nothing pending: speculate A on its own */
            maxA = *maxG;
            vcapA = *vcap;
            Baton T;
            rare_spec_prepare(&T, R, G, a0, rare_chunk_end(a0, end), *rare, *nrare, B, P);
            RareStats specst = {0};
#pragma omp parallel num_threads(T.P) if (T.P > 1)
            {
                int tid = 0;
#ifdef _OPENMP
                tid = omp_get_thread_num();
#endif
                rare_speculate_chunk(S, R, G, a0, mask, *rare, *nrare, maxG, vcap, B, &T, tid, &specst);
            }
            st->scan1 += specst.scan1;
            st->speculated += specst.speculated;
            st->bytes += specst.bytes;
            a1 = T.done;
            rc = T.rc;
            if (a1 == a0) {         /* storing failed at a0 itself: compute it exactly */
                unsigned g = grundy_rare(S, R, G, a0, mask, *rare, *nrare, st);
                if ((rc = store_value(S, G, a0, g, maxG, vcap))) return rc;
                if (rv_rare(g, mask) && (rc = rare_append(rare, nrare, cap, a0))) return rc;
                a1 = ++a0;
                continue;
            }
        }
        /* speculate B = [a1, b1) while A is checked; not after a failed store */
        long b1 = rc ? a1 : rare_chunk_end(a1, end), bend = a1;
        unsigned maxB = *maxG, vcapB = *vcap;   /* over G[0..a1), for when B becomes A */
        size_t need = 2 * (size_t)*vcap + 2;
        long cursor = a0, bad = a1;
        unsigned badval = 0;
        int oom = 0;
        RareStats specst = {0};
        Baton T;
        /* the team: threads 0 .. T.P-1 (at most nt - 1, so that one thread checks from the start) */
        rare_spec_prepare(&T, R, G, a1, b1, *rare, *nrare, B, P < nt ? P : nt - 1);
        /* the check of A runs step 1 on bytes where it can: g8 must cover A,
         * and nothing in B may change during the region (the speculation only
         * writes g8 from a1 on) */
        if (B && B->on && B->known < a1) rb_catch_up(B, G, a1, *rare, *nrare);
        const RareBytes *CB = B && B->on && B->known >= a1 && B->nr == *nrare ? B : NULL;
        long bytes_from = (*nrare ? 2 * (*rare)[*nrare - 1] : 0) + R->t + 1;
#pragma omp parallel num_threads(nt)
        {
            int tid = 0;
#ifdef _OPENMP
            tid = omp_get_thread_num();
#endif
            if (tid < T.P && b1 > a1)
                rare_speculate_chunk(S, R, G, a1, mask, *rare, *nrare, maxG, vcap, B, &T, tid, &specst);
            rare_check_worker(R, G, a1, mask, *rare, *nrare, need, CB, bytes_from, &cursor, &bad, &badval, &oom,
                              st);
        }
        if (b1 > a1) bend = T.done;
        st->scan1 += specst.scan1;
        st->speculated += specst.speculated;
        st->bytes += specst.bytes;
        if (oom) return -3;
        if (bad == a1) {            /* A is correct; B (if any) is the next chunk to check */
            if (progress) progress(a1, pctx);
            a0 = a1;
            a1 = bend;
            maxA = maxB;
            vcapA = vcapB;
            continue;
        }
        /* A is wrong at `bad`: keep G[a0..bad), store the correct G(bad), drop the rest */
        st->restarts++;
        *maxG = maxA;
        *vcap = vcapA;
        for (long i = a0; i < bad; i++)
            if (G[i] > *maxG) *maxG = G[i];
        while (*vcap <= *maxG) *vcap *= 2;
        if ((rc = store_value(S, G, bad, badval, maxG, vcap))) return rc;
        if (rv_rare(badval, mask) && (rc = rare_append(rare, nrare, cap, bad))) return rc;
        a0 = a1 = bad + 1;
        if (B && B->known > bad) B->known = bad;   /* g8 from `bad` on is stale */
    }
    return 0;
}

/** Default size of the speculation team (Baton), at most nt / 2.  Override: OCTAL_BATON=P.
 *  .6, grundy, couples to 10^7 on 24 threads: 6 and 5 are best (3.1-3.6 s), 4 is
 *  0.2-0.6 s slower, 8 no better. */
#define RARE_BATON_DEFAULT 6

/** Size of the speculation team for @p nt threads: OCTAL_BATON or RARE_BATON_DEFAULT, 1 .. nt. */
static int rare_baton_size(int nt) {
    int P = RARE_BATON_DEFAULT < nt / 2 ? RARE_BATON_DEFAULT : nt / 2;
    const char *e = getenv("OCTAL_BATON");
    if (e && *e) P = atoi(e);
    if (P > nt) P = nt;
    return P < 1 ? 1 : P;
}

/** The rare-value method is used while at most this fraction of heap sizes is rare. */
#define RARE_MAX_FRACTION 0.05

/**
 * @brief Compute G[from..to) with the rare-value algorithm (exact).
 *
 * Uses grundy_brute() until a mask makes at most RARE_MAX_FRACTION of the heap
 * sizes rare, then the rare-value algorithm.  The mask is (re)chosen at
 * n = 2048, 8192, 32768, ... (and at @p from, if that is past 2048).  Gives the
 * same values as grundy_brute() for every mask; the mask only affects the speed.
 *
 * With one thread it runs grundy_rare() for every heap size.  With more, from
 * RARE_SPEC_MIN on it uses speculation with overlapping check (rare_spec_run()):
 * step 1 on one thread, the full check of the previous chunk on the others at
 * the same time, in chunks of n/8 heap sizes (4096 .. 2^18).  The speculation
 * uses one byte per value where possible (RareBytes) and then runs on a team of
 * rare_baton_size() threads (baton passing, see Baton).
 *
 * Each call (and each mask checkpoint) starts and ends the pipeline with one
 * chunk speculated and one checked without overlap, so callers should pass
 * large ranges (compute() passes all of them at once).
 *
 * @param R        rule
 * @param G        array of at least @p to entries; G[0..from) valid
 * @param from     first heap size to compute
 * @param to       one past the last heap size
 * @param threads  1: plain algorithm; > 1: speculation with that many threads; <= 0: all cores
 * @param pairs    incremented by the number of two-heap pairs examined
 * @param st       statistics (may be NULL)
 * @param progress called now and then with the number of final heap sizes (may be NULL)
 * @param pctx     passed to @p progress
 * @return 0, -2 (value > 65535) or -3 (out of memory)
 */
static int extend_rare(const Rule *R, uint16_t *G, long from, long to, int threads, long long *pairs,
                       RareStats *st, ProgressFn progress, void *pctx) {
    RareStats dummy = {0};
    if (!st) st = &dummy;
    int nt = threads == 1 ? 1 : block_threads(threads);
    st->threads = nt;
    int P = rare_baton_size(nt);
    st->baton = P;
    unsigned maxG = 0, vcap = 2;
    for (long i = 0; i < from; i++)
        if (G[i] > maxG) maxG = G[i];
    while (vcap <= maxG) vcap *= 2;
    Scratch S = {0};
    if (scratch_reserve(&S, 2 * (size_t)vcap + 2)) return -3;
    long *rare = NULL, nrare = 0, cap = 0;
    unsigned mask = 0;
    int active = 0, rc = 0;
    RareBytes B = {0};
    long next_check = 2048;
    while (next_check <= from) next_check *= 4;
    for (long n = from; n < to && !rc; n++) {
        /* (re)choose the mask at the checkpoints, and when starting late */
        if ((n == from && n >= 2048) || n == next_check) {
            long rcnt;
            unsigned m = rv_choose_mask(G, n, vcap, &rcnt);
            active = rcnt <= RARE_MAX_FRACTION * n;
            if (active) {
                mask = m;
                nrare = rv_collect(G, n, mask, &rare, &cap);
                if (nrare < 0) { rc = -3; break; }
                if (nt > 1 && (rc = rb_setup(&B, R, mask, maxG, to))) break;
            }
            if (n == next_check) next_check *= 4;
        }
        if (active && nt > 1 && n >= RARE_SPEC_MIN) {
            /* speculation with overlapping check up to the next mask checkpoint,
             * where the loop re-chooses the mask */
            long end = to < next_check ? to : next_check;
            long long before = st->scan1 + st->scan2;
            rc = rare_spec_run(&S, R, G, n, end, mask, &rare, &nrare, &cap, &maxG, &vcap, nt, P, &B, st, progress, pctx);
            *pairs += st->scan1 + st->scan2 - before;
            n = end - 1;   /* the loop's n++ continues at end */
            continue;
        }
        long long before = st->scan1 + st->scan2;
        unsigned g = active ? grundy_rare(&S, R, G, n, mask, rare, nrare, st)
                            : grundy_brute(&S, R, G, n, pairs);
        if (active) *pairs += st->scan1 + st->scan2 - before;
        rc = store_value(&S, G, n, g, &maxG, &vcap);
        if (progress && (n & 65535) == 65535) progress(n + 1, pctx);
        if (!rc && active && n >= 1 && rv_rare(g, mask)) {
            if (nrare == cap) {
                long c = cap ? 2 * cap : 1024;
                long *p = realloc(rare, (size_t)c * sizeof *p);
                if (!p) { rc = -3; break; }
                rare = p;
                cap = c;
            }
            rare[nrare++] = n;
        }
    }
    st->active = active;
    st->mask = active ? mask : 0;
    st->nrare = active ? nrare : 0;
    free(rare);
    rb_free(&B);
    scratch_free(&S);
    return rc;
}

/* ------------------------------------------------------------ GPU (Vulkan) */
/* GPU backends share one driver, extend_device(); each backend only scans a
 * block (like block_scan()) on its device. */

/** The GPU method computes heap sizes below this on the CPU (extend_blocked()). */
#define GPU_MIN_N 32768
/** GPU block size: B ~ n0 / GPU_BLOCK_DIV.  Smaller than on the CPU, because the
 *  sequential block_finish() runs on one CPU core while the GPU waits. */
#define GPU_BLOCK_DIV 128
/** Largest GPU block: one workgroup per heap, one-dimensional dispatch. */
#define GPU_BLOCK_MAX 65535
/** Size of the buffer that receives the GPU option bitsets (bytes). */
#define GPU_MARK_BYTES (64L << 20)

/** Parameters of one GPU block scan; also the push constants of octal_gpu.comp
 *  and the kernel arguments in octal_rocm.hip (same order and types). */
typedef struct {
    int32_t n0, nwords, t;  /**< first heap of the block, bitset words per heap, last digit index */
    uint32_t km[3];         /**< bit k: digit d_k allows a two-heap split (k = 0..64) */
} GpuPush;

/**
 * @brief A GPU backend for extend_device() (Vulkan or ROCm).
 *
 * All functions are called with gpu_lock held, one computation at a time.
 */
typedef struct {
    /** Prepare the device once (later calls are cheap); copy its name into
     *  @p name unless NULL.  0 if usable, -1 if not. */
    int (*init)(char *name, int len);
    /** Start a computation of heap sizes < @p to: upload G[0..from), allocate
     *  @p mark_bytes for the option bitsets.  0, -3 or -4. */
    int (*begin)(const uint16_t *G, long from, long to, size_t mark_bytes);
    /** Scan one block (see octal_gpu.comp) and return its B * nwords bitset
     *  words in host memory, valid until the next call; NULL on error. */
    const uint32_t *(*scan)(const GpuPush *pc, long B);
    /** Upload the finished values G[n0..n0+B).  0, or -4 on error. */
    int (*append)(const uint16_t *G, long n0, long B);
    /** Free what begin() allocated. */
    void (*end)(void);
} Device;

/** Serialises all GPU use: one GPU computation at a time per process. */
static pthread_mutex_t gpu_lock = PTHREAD_MUTEX_INITIALIZER;

#ifdef OCTAL_VULKAN
#include <vulkan/vulkan.h>
#include "octal_gpu_spv.h"   /* SPIR-V of octal_gpu.comp: const uint32_t octal_gpu_spv[] */

/**
 * @brief The Vulkan objects shared by all GPU computations.
 *
 * Created once, on first use, by gpu_init() and kept until the process exits.
 * Every use is serialised by gpu_lock.
 */
static struct {
    int state;                        /**< 0 not tried yet, 1 ready, -1 unavailable */
    VkInstance inst;
    VkPhysicalDevice phys;
    VkDevice dev;
    VkQueue queue;
    VkPhysicalDeviceMemoryProperties mem;
    VkDescriptorSetLayout dsl;
    VkPipelineLayout layout;
    VkPipeline pipe;
    VkDescriptorPool dpool;
    VkCommandPool cpool;
    VkCommandBuffer cmd;
    VkFence fence;
    char name[256];                   /**< device name, e.g. "AMD Radeon ..." */
} gpu;

/**
 * @brief Create the Vulkan device, compute pipeline and command buffer (once).
 *
 * Picks the first discrete GPU, else the first integrated or virtual GPU, that
 * has a compute queue and supports 256 invocations per workgroup.  CPU devices
 * (llvmpipe) are skipped unless the environment variable OCTAL_GPU_ALLOW_CPU is
 * set (useful for testing without a GPU).  Call with gpu_lock held.
 *
 * @return 0 if the GPU is ready, -1 if no usable device was found
 */
static int gpu_init(void) {
    if (gpu.state) return gpu.state > 0 ? 0 : -1;
    gpu.state = -1;
    VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                             .pApplicationName = "octal-explorer", .apiVersion = VK_API_VERSION_1_1};
    VkInstanceCreateInfo ici = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app};
    if (vkCreateInstance(&ici, NULL, &gpu.inst) != VK_SUCCESS) return -1;

    uint32_t nd = 0;
    vkEnumeratePhysicalDevices(gpu.inst, &nd, NULL);
    VkPhysicalDevice devs[16];
    if (nd > 16) nd = 16;
    vkEnumeratePhysicalDevices(gpu.inst, &nd, devs);
    int allow_cpu = getenv("OCTAL_GPU_ALLOW_CPU") != NULL;
    int best = -1, best_score = 0;
    uint32_t best_q = 0;
    for (uint32_t i = 0; i < nd; i++) {
        VkPhysicalDeviceProperties pr;
        vkGetPhysicalDeviceProperties(devs[i], &pr);
        int score = pr.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 4
                  : pr.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 3
                  : pr.deviceType == VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU ? 2
                  : allow_cpu ? 1 : 0;
        if (pr.limits.maxComputeWorkGroupSize[0] < 256 || pr.limits.maxComputeWorkGroupInvocations < 256 ||
            pr.limits.maxComputeSharedMemorySize < 8192)
            score = 0;
        uint32_t nq = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(devs[i], &nq, NULL);
        VkQueueFamilyProperties qp[32];
        if (nq > 32) nq = 32;
        vkGetPhysicalDeviceQueueFamilyProperties(devs[i], &nq, qp);
        uint32_t q = UINT32_MAX;
        for (uint32_t j = 0; j < nq; j++)
            if (qp[j].queueFlags & VK_QUEUE_COMPUTE_BIT) { q = j; break; }
        if (q == UINT32_MAX) score = 0;
        if (score > best_score) {
            best = (int)i;
            best_score = score;
            best_q = q;
            snprintf(gpu.name, sizeof gpu.name, "%s", pr.deviceName);
        }
    }
    if (best < 0) return -1;
    gpu.phys = devs[best];
    vkGetPhysicalDeviceMemoryProperties(gpu.phys, &gpu.mem);

    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                                   .queueFamilyIndex = best_q, .queueCount = 1, .pQueuePriorities = &prio};
    VkDeviceCreateInfo dci = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
                              .queueCreateInfoCount = 1, .pQueueCreateInfos = &qci};
    if (vkCreateDevice(gpu.phys, &dci, NULL, &gpu.dev) != VK_SUCCESS) return -1;
    vkGetDeviceQueue(gpu.dev, best_q, 0, &gpu.queue);

    VkShaderModuleCreateInfo smi = {.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                                    .codeSize = sizeof octal_gpu_spv, .pCode = octal_gpu_spv};
    VkShaderModule sm;
    if (vkCreateShaderModule(gpu.dev, &smi, NULL, &sm) != VK_SUCCESS) return -1;

    VkDescriptorSetLayoutBinding b[2] = {
        {.binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1,
         .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT},
        {.binding = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1,
         .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT}};
    VkDescriptorSetLayoutCreateInfo dli = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                                           .bindingCount = 2, .pBindings = b};
    VkPushConstantRange pcr = {.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT, .size = sizeof(GpuPush)};
    if (vkCreateDescriptorSetLayout(gpu.dev, &dli, NULL, &gpu.dsl) != VK_SUCCESS) return -1;
    VkPipelineLayoutCreateInfo pli = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                                      .setLayoutCount = 1, .pSetLayouts = &gpu.dsl,
                                      .pushConstantRangeCount = 1, .pPushConstantRanges = &pcr};
    if (vkCreatePipelineLayout(gpu.dev, &pli, NULL, &gpu.layout) != VK_SUCCESS) return -1;
    VkComputePipelineCreateInfo cpi = {
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage = {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                  .stage = VK_SHADER_STAGE_COMPUTE_BIT, .module = sm, .pName = "main"},
        .layout = gpu.layout};
    VkResult r = vkCreateComputePipelines(gpu.dev, VK_NULL_HANDLE, 1, &cpi, NULL, &gpu.pipe);
    vkDestroyShaderModule(gpu.dev, sm, NULL);
    if (r != VK_SUCCESS) return -1;

    VkDescriptorPoolSize ps = {.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 2};
    VkDescriptorPoolCreateInfo dpi = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
                                      .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
                                      .maxSets = 1, .poolSizeCount = 1, .pPoolSizes = &ps};
    if (vkCreateDescriptorPool(gpu.dev, &dpi, NULL, &gpu.dpool) != VK_SUCCESS) return -1;
    VkCommandPoolCreateInfo cpci = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                                    .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
                                    .queueFamilyIndex = best_q};
    if (vkCreateCommandPool(gpu.dev, &cpci, NULL, &gpu.cpool) != VK_SUCCESS) return -1;
    VkCommandBufferAllocateInfo cbi = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                                       .commandPool = gpu.cpool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                                       .commandBufferCount = 1};
    if (vkAllocateCommandBuffers(gpu.dev, &cbi, &gpu.cmd) != VK_SUCCESS) return -1;
    VkFenceCreateInfo fci = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    if (vkCreateFence(gpu.dev, &fci, NULL, &gpu.fence) != VK_SUCCESS) return -1;
    gpu.state = 1;
    return 0;
}

/** @brief A storage buffer in host-visible memory, mapped for its whole life. */
typedef struct {
    VkBuffer buf;
    VkDeviceMemory mem;
    void *map;
} GpuBuf;

/**
 * @brief Create a host-visible, host-coherent storage buffer of @p size bytes and map it.
 *
 * Prefers memory that is also device-local (all memory on integrated GPUs;
 * resizable BAR on discrete ones), and with @p cached also host-cached, for
 * buffers the CPU reads back.  Call with gpu_lock held.
 *
 * @return 0, or -1 if it cannot be allocated
 */
static int gpu_buf_create(GpuBuf *g, VkDeviceSize size, int cached) {
    VkBufferCreateInfo bi = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = size,
                             .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                             .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
    if (vkCreateBuffer(gpu.dev, &bi, NULL, &g->buf) != VK_SUCCESS) return -1;
    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(gpu.dev, g->buf, &mr);
    const VkMemoryPropertyFlags base = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    VkMemoryPropertyFlags extra = cached ? VK_MEMORY_PROPERTY_HOST_CACHED_BIT : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    VkMemoryPropertyFlags tries[2] = {base | extra, base};
    for (int t = 0; t < 2; t++)
        for (uint32_t i = 0; i < gpu.mem.memoryTypeCount; i++) {
            if (!(mr.memoryTypeBits & (1u << i))) continue;
            if ((gpu.mem.memoryTypes[i].propertyFlags & tries[t]) != tries[t]) continue;
            VkMemoryAllocateInfo ai = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                                       .allocationSize = mr.size, .memoryTypeIndex = i};
            if (vkAllocateMemory(gpu.dev, &ai, NULL, &g->mem) != VK_SUCCESS) continue;
            if (vkBindBufferMemory(gpu.dev, g->buf, g->mem, 0) == VK_SUCCESS &&
                vkMapMemory(gpu.dev, g->mem, 0, VK_WHOLE_SIZE, 0, &g->map) == VK_SUCCESS)
                return 0;
            vkFreeMemory(gpu.dev, g->mem, NULL);
            g->mem = VK_NULL_HANDLE;
        }
    vkDestroyBuffer(gpu.dev, g->buf, NULL);
    g->buf = VK_NULL_HANDLE;
    return -1;
}

/** Unmap and free @p g (no-op for a zeroed GpuBuf).  Call with gpu_lock held. */
static void gpu_buf_free(GpuBuf *g) {
    if (g->mem) {
        vkUnmapMemory(gpu.dev, g->mem);
        vkFreeMemory(gpu.dev, g->mem, NULL);
    }
    if (g->buf) vkDestroyBuffer(gpu.dev, g->buf, NULL);
    memset(g, 0, sizeof *g);
}

/** Vulkan state of the running computation (one at a time, under gpu_lock). */
static GpuBuf vk_gb, vk_mb;
static VkDescriptorSet vk_ds;

/** Device::init for Vulkan. */
static int vk_init(char *name, int len) {
    if (gpu_init()) return -1;
    if (name) snprintf(name, (size_t)len, "%s", gpu.name);
    return 0;
}

/** Device::end for Vulkan: free the buffers of the computation. */
static void vk_end(void) {
    if (vk_ds) vkFreeDescriptorSets(gpu.dev, gpu.dpool, 1, &vk_ds);
    vk_ds = VK_NULL_HANDLE;
    gpu_buf_free(&vk_gb);
    gpu_buf_free(&vk_mb);
}

/**
 * Device::begin for Vulkan.  The nim-values live in a host-visible buffer (two
 * uint16 per uint32, little-endian host assumed): no copies on integrated GPUs
 * and discrete GPUs with resizable BAR, reads over PCIe on other discrete GPUs.
 */
static int vk_begin(const uint16_t *G, long from, long to, size_t mark_bytes) {
    if (gpu_buf_create(&vk_gb, (VkDeviceSize)((to + 1) / 2) * 4, 0) ||
        gpu_buf_create(&vk_mb, mark_bytes, 1)) {
        vk_end();
        return -3;
    }
    VkDescriptorSetAllocateInfo dai = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                                       .descriptorPool = gpu.dpool, .descriptorSetCount = 1,
                                       .pSetLayouts = &gpu.dsl};
    if (vkAllocateDescriptorSets(gpu.dev, &dai, &vk_ds) != VK_SUCCESS) {
        vk_ds = VK_NULL_HANDLE;
        vk_end();
        return -4;
    }
    VkDescriptorBufferInfo dbi[2] = {{vk_gb.buf, 0, VK_WHOLE_SIZE}, {vk_mb.buf, 0, VK_WHOLE_SIZE}};
    VkWriteDescriptorSet w[2];
    for (int i = 0; i < 2; i++)
        w[i] = (VkWriteDescriptorSet){.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = vk_ds,
                                      .dstBinding = (uint32_t)i, .descriptorCount = 1,
                                      .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                      .pBufferInfo = &dbi[i]};
    vkUpdateDescriptorSets(gpu.dev, 2, w, 0, NULL);
    memcpy(vk_gb.map, G, (size_t)from * sizeof *G);
    return 0;
}

/** Device::scan for Vulkan: run octal_gpu.comp on one block and wait for it. */
static const uint32_t *vk_scan(const GpuPush *pc, long B) {
    VkCommandBufferBeginInfo bi = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                                   .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
    if (vkResetCommandBuffer(gpu.cmd, 0) != VK_SUCCESS || vkBeginCommandBuffer(gpu.cmd, &bi) != VK_SUCCESS)
        return NULL;
    vkCmdBindPipeline(gpu.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, gpu.pipe);
    vkCmdBindDescriptorSets(gpu.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, gpu.layout, 0, 1, &vk_ds, 0, NULL);
    vkCmdPushConstants(gpu.cmd, gpu.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof *pc, pc);
    vkCmdDispatch(gpu.cmd, (uint32_t)B, 1, 1);
    /* make the shader's writes visible to the host after the fence */
    VkMemoryBarrier mb = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                          .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT, .dstAccessMask = VK_ACCESS_HOST_READ_BIT};
    vkCmdPipelineBarrier(gpu.cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &mb,
                         0, NULL, 0, NULL);
    if (vkEndCommandBuffer(gpu.cmd) != VK_SUCCESS) return NULL;
    VkSubmitInfo si = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1,
                       .pCommandBuffers = &gpu.cmd};
    if (vkQueueSubmit(gpu.queue, 1, &si, gpu.fence) != VK_SUCCESS) return NULL;
    VkResult r = vkWaitForFences(gpu.dev, 1, &gpu.fence, VK_TRUE, UINT64_MAX);
    vkResetFences(gpu.dev, 1, &gpu.fence);
    return r == VK_SUCCESS ? vk_mb.map : NULL;
}

/** Device::append for Vulkan: the finished block, for the next blocks to read. */
static int vk_append(const uint16_t *G, long n0, long B) {
    memcpy((uint16_t *)vk_gb.map + n0, G + n0, (size_t)B * sizeof *G);
    return 0;
}

static const Device vulkan_device = {vk_init, vk_begin, vk_scan, vk_append, vk_end};
#define VULKAN_DEVICE (&vulkan_device)
#else
#define VULKAN_DEVICE NULL
#endif /* OCTAL_VULKAN */

/* ----------------------------------------------------------------- ROCm (HIP) */

#ifdef OCTAL_ROCM
/* Implemented in octal_rocm.hip (compiled with hipcc). */
int octal_rocm_dev_init(char *name, int len);
int octal_rocm_dev_begin(const uint16_t *G, long from, long to, size_t mark_bytes);
const uint32_t *octal_rocm_dev_scan(const GpuPush *pc, long B);
int octal_rocm_dev_append(const uint16_t *G, long n0, long B);
void octal_rocm_dev_end(void);
static const Device rocm_device = {octal_rocm_dev_init, octal_rocm_dev_begin, octal_rocm_dev_scan,
                                   octal_rocm_dev_append, octal_rocm_dev_end};
#define ROCM_DEVICE (&rocm_device)
#else
#define ROCM_DEVICE NULL
#endif /* OCTAL_ROCM */

/* ------------------------------------------------------- GPU block driver */

/** Name of @p D's GPU into @p name (0), or -4 if @p D is NULL or has no usable GPU. */
static int device_name(const Device *D, char *name, int len) {
    if (!D) return -4;
    pthread_mutex_lock(&gpu_lock);
    int rc = D->init(name, len) ? -4 : 0;
    pthread_mutex_unlock(&gpu_lock);
    return rc;
}

/**
 * @brief Compute G[from..to) with the blocked method, scanning blocks on a GPU.
 *
 * Like extend_blocked(), but block_scan() runs on the device @p D (Vulkan
 * shader octal_gpu.comp or HIP kernel octal_rocm.hip): one workgroup per heap of
 * the block, option bitsets in shared memory.  block_finish() stays on one CPU
 * core; the blocks are smaller (B ~ n0/128) to keep that sequential part small.
 * Heap sizes below GPU_MIN_N are computed with extend_blocked() on all CPU
 * cores.  Calls are serialised by gpu_lock.
 *
 * @param D      device backend, or NULL if not compiled in
 * @param R      rule
 * @param G      array of at least @p to entries; G[0..from) valid
 * @param from   first heap size to compute
 * @param to     one past the last heap size
 * @param pairs  incremented by the number of two-heap splits examined
 * @return 0, -2 (value > 65535), -3 (out of memory) or -4 (no usable GPU, or
 *         a GPU error); G is valid up to where it stopped
 */
static int extend_device(const Device *D, const Rule *R, uint16_t *G, long from, long to,
                         long long *pairs) {
    if (device_name(D, NULL, 0)) return -4;
    if (from < GPU_MIN_N) {
        long e = to < GPU_MIN_N ? to : GPU_MIN_N;
        int rc = extend_blocked(R, G, from, e, 0, pairs);
        if (rc || e == to) return rc;
        from = e;
    }
    pthread_mutex_lock(&gpu_lock);
    Scratch S = {0};
    int rc = D->begin(G, from, to, GPU_MARK_BYTES);
    if (rc) {
        pthread_mutex_unlock(&gpu_lock);
        return rc;
    }
    unsigned maxG = 0, vcap = 2;
    for (long i = 0; i < from; i++)
        if (G[i] > maxG) maxG = G[i];
    while (vcap <= maxG) vcap *= 2;
    if (scratch_reserve(&S, 2 * (size_t)vcap + 2)) rc = -3;
    GpuPush pc = {.t = R->t};
    for (int k = 0; k <= R->t; k++)
        if (R->d[k] & 4) pc.km[k / 32] |= 1u << (k % 32);

    for (long n = from; n < to && !rc;) {
        int nwords = (int)((vcap + 31) / 32);
        long B = n / GPU_BLOCK_DIV;
        if (B > GPU_BLOCK_MAX) B = GPU_BLOCK_MAX;
        if (B > GPU_MARK_BYTES / (4L * nwords)) B = GPU_MARK_BYTES / (4L * nwords);
        if (B > to - n) B = to - n;
        if (B < 1) B = 1;
        pc.n0 = (int32_t)n;
        pc.nwords = nwords;
        unsigned vcap0 = vcap;
        const uint32_t *marks = D->scan(&pc, B);
        if (!marks) { rc = -4; break; }
        rc = block_finish(&S, R, G, n, B, vcap0, marks, 1, &maxG, &vcap, pairs);
        if (rc) break;
        if (D->append(G, n, B)) { rc = -4; break; }   /* the next blocks read them */
        n += B;
    }
    D->end();
    scratch_free(&S);
    pthread_mutex_unlock(&gpu_lock);
    return rc;
}


/** Progress state of compute() (for compute_progress()). */
typedef struct {
    long N;
    Method method;
    double t0;
} ProgressCtx;

/** Show "n = ... / N" on stderr (compute() only calls it when stderr is a terminal). */
static void compute_progress(long n, void *ctx) {
    const ProgressCtx *p = ctx;
    fprintf(stderr, "\r  n = %ld / %ld  (%.0f%%, %s, %.0f s)   ", n, p->N, 100.0 * n / p->N,
            method_name[p->method], now() - p->t0);
}

/**
 * @brief Compute G(0) .. G(N-1) (command-line program).
 *
 * With METHOD_BLOCKED, METHOD_VULKAN or METHOD_ROCM it uses extend_blocked()
 * or extend_device() in chunks.  Otherwise grundy_brute(), or with
 * METHOD_SPARSE grundy_sparse() when a mask makes fewer than 25% of heap
 * sizes rare.  The mask is re-chosen at
 * n = 2048, 8192, 32768, ...  Shows progress on stderr when it is a terminal.
 * Dies if a value exceeds 65535 or memory runs out (command-line use only; the
 * library uses octal_extend(), which returns error codes instead).
 *
 * @param R             rule
 * @param N             number of heap sizes
 * @param method        how to compute
 * @param threads       threads for METHOD_BLOCKED, <= 0 for all
 * @param progress      show progress on stderr (only if it is a terminal)
 * @return the sequence and statistics; the caller frees Result.G
 */
Result compute(const Rule *R, long N, Method method, int threads, int progress) {
    Result res = {0};
    res.N = N;
    res.G = xcalloc((size_t)N, sizeof *res.G);
    res.method = method;
    uint16_t *G = res.G;
    int allow_sparse = method == METHOD_SPARSE;
    if (method != METHOD_BRUTE && method != METHOD_SPARSE) {
        double t0 = now();
        int show = progress && isatty(2);
        res.threads = block_threads(threads);
        ProgressCtx pc = {N, method, t0};
        for (long n = 0; n < N;) {
            /* the rare-value method in one call: every call drains its pipeline
             * of speculation and check (one chunk speculated and one checked
             * without overlap), which cost about 1 s of 6 for .6 to 10^7; it
             * reports progress itself */
            long e = method == METHOD_RARE ? N : n + (n < 65536 ? 65536 : n / 8);
            if (e > N) e = N;
            int rc = method == METHOD_VULKAN ? extend_device(VULKAN_DEVICE, R, G, n, e, &res.pairs)
                   : method == METHOD_ROCM   ? extend_device(ROCM_DEVICE, R, G, n, e, &res.pairs)
                   : method == METHOD_RARE   ? extend_rare(R, G, n, e, threads, &res.pairs, &res.rare,
                                                             show ? compute_progress : NULL, &pc)
                                             : extend_blocked(R, G, n, e, threads, &res.pairs);
            if (rc == -2) die("nim-value exceeds 65535; not supported");
            if (rc == -4 && method == METHOD_VULKAN) die("no usable GPU (Vulkan), or not built with -DOCTAL_VULKAN");
            if (rc == -4) die("no usable ROCm GPU, or not built with -DOCTAL_ROCM");
            if (rc) die("out of memory");
            n = e;
            if (show) compute_progress(n, &pc);
        }
        if (show) fprintf(stderr, "\r%78s\r", "");
        res.secs = now() - t0;
        return res;
    }
    unsigned maxG = 0, vcap = 2;
    Scratch S = {0};
    if (scratch_reserve(&S, 2 * vcap + 2)) die("out of memory");
    long *rare = NULL, nrare = 0, rare_cap = 0;
    int sparse = 0;
    unsigned mask = 0;
    long next_check = 2048;
    double t0 = now(), last = t0;
    int show = progress && isatty(2);

    for (long n = 0; n < N; n++) {
        unsigned g = sparse ? grundy_sparse(&S, R, G, n, mask, rare, nrare, &res.pairs)
                            : grundy_brute(&S, R, G, n, &res.pairs);
        if (g > 65535) die("nim-value exceeds 65535; not supported");
        G[n] = (uint16_t)g;
        if (g > maxG) {
            maxG = g;
            while (vcap <= maxG) vcap *= 2;
            if (scratch_reserve(&S, 2 * (size_t)vcap + 2)) die("out of memory");
        }
        if (sparse && is_rare(g, mask)) {
            if (nrare == rare_cap) {
                rare_cap = rare_cap ? 2 * rare_cap : 1024;
                rare = realloc(rare, rare_cap * sizeof *rare);
                if (!rare) die("out of memory");
            }
            rare[nrare++] = n;
        }
        /* (re)choose the mask at n = 2048, 8192, 32768, ... */
        if (allow_sparse && n + 1 == next_check && n + 1 < N) {
            long rc;
            unsigned m = choose_mask(G, n + 1, vcap, &rc);
            if ((double)rc / (n + 1) < 0.25) {
                if (!sparse || m != mask) {
                    mask = m;
                    nrare = 0;
                    for (long i = 1; i <= n; i++)
                        if (is_rare(G[i], mask)) {
                            if (nrare == rare_cap) {
                                rare_cap = rare_cap ? 2 * rare_cap : 1024;
                                while (rare_cap < nrare + 1) rare_cap *= 2;
                                rare = realloc(rare, rare_cap * sizeof *rare);
                                if (!rare) die("out of memory");
                            }
                            rare[nrare++] = i;
                        }
                }
                sparse = 1;
            } else {
                sparse = 0;
            }
            next_check *= 4;
        }
        if (show && (n & 0xfff) == 0) {
            double t = now();
            if (t - last > 0.5) {
                double rate = n / (t - t0);
                fprintf(stderr, "\r  n = %ld / %ld  (%.0f%%, max %u, %s, %.0f s)   ", n, N,
                        100.0 * n / N, maxG, sparse ? "sparse" : "brute", t - t0);
                (void)rate;
                last = t;
            }
        }
    }
    if (show) fprintf(stderr, "\r%78s\r", "");
    res.secs = now() - t0;
    res.sparse = sparse;
    res.mask = mask;
    res.nrare = sparse ? nrare : 0;
    free(rare);
    scratch_free(&S);
    return res;
}

/* ------------------------------------------------------------ periodicity */

/**
 * @brief Find an eventual period of G[0..N), optionally with a saltus.
 *
 * For p = 1, 2, ..., N/2 the preperiod i is the smallest index with
 * G(n+p) = G(n) + s for all i <= n < N-p (found by scanning backwards).  The
 * saltus s is 0, or, if @p allow_saltus is set, the difference G(N-1) -
 * G(N-1-p) at the end of the data (negative differences are treated as 0).
 * s > 0 is *arithmetic periodicity*, typical of hexadecimal games.
 *
 * Octal periodicity theorem (Winning Ways, ch. 4): if G(n+p) = G(n) for all
 * i <= n < 2i + p + t, where t is the last nonzero code digit, the sequence is
 * periodic from i on.  So p is *proved* once N >= 2i + 2p + t; this is only
 * claimed for plain octal codes (no d0, no hex digits, no named rules).
 *
 * Arithmetic periods (s > 0) are proved with Austin's Theorem 6.8 when its
 * hypotheses hold (see austin_check()), possibly for a multiple of the
 * period and saltus; otherwise they stay apparent.
 *
 * Returns the first proved period; otherwise the smallest p with N - i >= 3p
 * (and, for s > 0, also N - i >= N/4) as an *apparent* period (multiples of it
 * are skipped, keeping the search linear for periodic data); otherwise
 * found = 0.
 */
static void austin_prove(const uint16_t *G, long N, const Rule *R, Period *P);

Period find_period(const uint16_t *G, long N, const Rule *R, int allow_saltus) {
    int can_prove = R->special == SP_NONE && R->d[0] == 0 && !R->hex;
    Period ap = {0};
    for (long p = 1; p <= N / 2; p++) {
        if (ap.found && p % ap.per == 0) continue;
        long s = allow_saltus ? (long)G[N - 1] - (long)G[N - 1 - p] : 0;
        if (s < 0) s = 0;
        long n = N - p - 1;
        while (n >= 0 && (long)G[n + p] - (long)G[n] == s) n--;
        long i = n + 1;
        if (s == 0 && can_prove && (long long)N >= 2LL * i + 2LL * p + R->t) {
            Period r = {i, p, 1, 1, 0, 1, p, 0};
            return r;
        }
        /* apparent: at least 3 periods; with a saltus also at least N/4 values,
         * since short runs like G rising by 1 three times occur by chance */
        long need = 3 * p;
        if (s > 0 && need < N / 4) need = N / 4;
        if (!ap.found && N - i >= need) {
            ap.pre = i; ap.per = p; ap.found = 1; ap.saltus = s;
        }
    }
    if (ap.found && ap.saltus > 0) austin_prove(G, N, R, &ap);
    return ap;
}

/**
 * @brief Check the hypotheses of Austin's Theorem 6.8 for given e, p, s.
 *
 * R. B. Austin, *Impartial and partisan games*, MSc thesis, University of
 * Calgary, 1976, Theorem 6.8 (p. 122): let d0.d1...dt (t >= 1) be a hexadecimal
 * code (digits <= 15, so a move leaves at most three heaps), e the last
 * irregular index, p >= t+2 and s = 2^k >= 1.  If
 *   (1) G(i+p) = G(i) + s for all e < i < e+7p+t,
 *   (2) G(i) < s for all i <= e,
 *   (3) G(i) < 2s for all i <= e+p,
 *   (4) either some d_{2v+1} and some d_{2w} both contain 8 and every g with
 *       0 <= g < 2s equals G(i) for some i > 0, or some d_u contains 8 and
 *       every g with 0 <= g < 2s equals G(2v+1) = G(2w) for some odd heap
 *       2v+1 and even heap 2w >= 0,
 * then G(i+p) = G(i) + s for all i > e.  ("d contains 8": 8 occurs in the
 * binary expansion of d, i.e. a move may leave three heaps.)
 *
 * @return 1 if all hypotheses hold within G[0..N), else 0
 */
static int austin_check(const uint16_t *G, long N, const Rule *R, long e, long p, long s) {
    int t = R->t < 1 ? 1 : R->t;
    if (s < 1 || (s & (s - 1)) || p < t + 2) return 0;
    if ((long long)e + 8LL * p + t > N) return 0;         /* (1) needs G up to e+8p+t-1 */
    for (long i = e + 1; i < e + 7 * p + t; i++)           /* (1) */
        if ((long)G[i + p] - (long)G[i] != s) return 0;
    for (long i = 0; i <= e; i++)                          /* (2) */
        if (G[i] >= s) return 0;
    for (long i = 0; i <= e + p; i++)                      /* (3) */
        if (G[i] >= 2 * s) return 0;
    int odd8 = 0, even8 = 0;                               /* (4) */
    for (int u = 0; u <= R->t; u++)
        if (R->d[u] & 8) { if (u & 1) odd8 = 1; else even8 = 1; }
    if (!odd8 && !even8) return 0;
    unsigned char *f = xcalloc((size_t)(2 * s), 1);        /* bit 1: some i > 0, 2: odd, 4: even */
    for (long i = 0; i < N; i++)
        if (G[i] < 2 * s) f[G[i]] |= (unsigned char)((i > 0 ? 1 : 0) | (i & 1 ? 2 : 4));
    int a = odd8 && even8, b = 1;
    for (long g = 0; g < 2 * s; g++) {
        if (!(f[g] & 1)) a = 0;
        if ((f[g] & 6) != 6) b = 0;
    }
    free(f);
    return a || b;
}

/**
 * @brief Try to prove an apparent arithmetic period with Austin's Theorem 6.8.
 *
 * The theorem's hypotheses (s a power of 2, p >= t+2, conditions (2) and (3))
 * may only hold for a multiple m*p, m*s of the least period and saltus, as
 * Austin notes for .F8 (period 6, saltus 4, proved with 12 and 8).  So
 * m = 1, 2, ... are tried while the data suffice.  On success P->proven is
 * set and P->proof_per / P->proof_saltus hold the certified multiple.
 * Only plain take-and-break codes qualify (no named rules).
 */
static void austin_prove(const uint16_t *G, long N, const Rule *R, Period *P) {
    if (R->special != SP_NONE || !P->found || P->saltus < 1) return;
    long e = P->pre - 1;
    int t = R->t < 1 ? 1 : R->t;
    for (long m = 1; (long long)e + 8LL * m * P->per + t <= N; m++)
        if (austin_check(G, N, R, e, m * P->per, m * P->saltus)) {
            P->proven = 1;
            P->proof = 2;
            P->proof_per = m * P->per;
            P->proof_saltus = m * P->saltus;
            return;
        }
}

/* ---------------------------------------------------------- library API */

/** See octal.h.  Human-readable name of @p code; -1 if it does not parse. */
int octal_rule_name(const char *code, char *name, int len) {
    Rule R;
    if (parse_rule(code, &R)) return -1;
    snprintf(name, (size_t)len, "%s", R.name);
    return 0;
}

/**
 * See octal.h.  Fill G[from..to) given G[0..from) with grundy_brute().
 * Uses its own Scratch, freed before returning, so it is reentrant: calls in
 * different threads may run at the same time (on different arrays G).
 * Returns 0, -1 (bad code), -2 (value > 65535) or -3 (out of memory).
 */
int octal_extend(const char *code, uint16_t *G, long from, long to) {
    Rule R;
    if (parse_rule(code, &R)) return -1;
    unsigned maxG = 0, vcap = 2;
    for (long i = 0; i < from; i++)
        if (G[i] > maxG) maxG = G[i];
    while (vcap <= maxG) vcap *= 2;
    Scratch S = {0};
    int rc = 0;
    if (scratch_reserve(&S, 2 * (size_t)vcap + 2)) return -3;
    long long pairs = 0;
    for (long n = from; n < to; n++) {
        unsigned g = grundy_brute(&S, &R, G, n, &pairs);
        if (g > 65535) { rc = -2; break; }
        G[n] = (uint16_t)g;
        if (g > maxG) {
            maxG = g;
            while (vcap <= maxG) vcap *= 2;
            if (scratch_reserve(&S, 2 * (size_t)vcap + 2)) { rc = -3; break; }
        }
    }
    scratch_free(&S);
    return rc;
}

/** See octal.h.  Fill G[from..to) with extend_blocked() using @p threads
 *  threads (<= 0: all).  Returns 0, -1, -2 or -3 like octal_extend(). */
int octal_extend_blocked(const char *code, uint16_t *G, long from, long to, int threads) {
    Rule R;
    if (parse_rule(code, &R)) return -1;
    long long pairs = 0;
    return extend_blocked(&R, G, from, to, threads, &pairs);
}

/** See octal.h.  Fill G[from..to) with extend_rare() on @p threads threads
 *  (1: plain algorithm, otherwise speculation).  Returns 0, -1, -2, -3. */
int octal_extend_rare(const char *code, uint16_t *G, long from, long to, int threads) {
    Rule R;
    if (parse_rule(code, &R)) return -1;
    long long pairs = 0;
    return extend_rare(&R, G, from, to, threads, &pairs, NULL, NULL, NULL);
}

/** See octal.h.  Fill G[from..to) with extend_device() on Vulkan.  Returns 0,
 *  -1, -2, -3 like octal_extend(), or -4 if no GPU is usable. */
int octal_extend_gpu(const char *code, uint16_t *G, long from, long to) {
    Rule R;
    if (parse_rule(code, &R)) return -1;
    long long pairs = 0;
    return extend_device(VULKAN_DEVICE, &R, G, from, to, &pairs);
}

/** See octal.h.  Name of the GPU used by octal_extend_gpu(); 0, or -4 if none. */
int octal_gpu_name(char *name, int len) {
    return device_name(VULKAN_DEVICE, name, len);
}

/** See octal.h.  Like octal_extend_gpu(), on ROCm (HIP). */
int octal_extend_rocm(const char *code, uint16_t *G, long from, long to) {
    Rule R;
    if (parse_rule(code, &R)) return -1;
    long long pairs = 0;
    return extend_device(ROCM_DEVICE, &R, G, from, to, &pairs);
}

/** See octal.h.  Name of the GPU used by octal_extend_rocm(); 0, or -4 if none. */
int octal_rocm_name(char *name, int len) {
    return device_name(ROCM_DEVICE, name, len);
}

/** See octal.h.  Wraps find_period() without saltus: returns 0 none,
 *  1 apparent, 2 proved (-1 bad code); out[0] = preperiod, out[1] = period. */
int octal_period(const char *code, const uint16_t *G, long N, long *out) {
    Rule R;
    if (parse_rule(code, &R)) return -1;
    Period P = find_period(G, N, &R, 0);
    out[0] = P.pre;
    out[1] = P.per;
    return P.found ? (P.proven ? 2 : 1) : 0;
}

/** See octal.h.  Like octal_period() but also detects arithmetic periodicity
 *  and proves it with Austin's Theorem 6.8 where possible.  out[0] = preperiod,
 *  out[1] = period, out[2] = saltus, out[3] / out[4] = period / saltus that
 *  the proof certifies.  Returns 0 none, 1 apparent, 2 proved by the octal
 *  periodicity theorem, 3 proved by Austin's Theorem 6.8, -1 bad code. */
int octal_period_ex(const char *code, const uint16_t *G, long N, long *out) {
    Rule R;
    if (parse_rule(code, &R)) return -1;
    Period P = find_period(G, N, &R, 1);
    out[0] = P.pre;
    out[1] = P.per;
    out[2] = P.saltus;
    out[3] = P.proven ? P.proof_per : 0;
    out[4] = P.proven ? P.proof_saltus : 0;
    return P.found ? (P.proven ? 1 + P.proof : 1) : 0;
}

