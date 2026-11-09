/**
 * @file octal.c
 * @brief octal-explorer: explore nim-sequences of octal and hexadecimal take-and-break games.
 *
 *   octal .007 -n 200000            Treblecross, 200k heap sizes
 *   octal .6 -n 1000000 -o officers  Officers into directory "officers"
 *   octal grundy -n 50000            Grundy's game (split into two unequal heaps)
 *   octal couples -n 50000           Couples-are-forever (split any heap except 2)
 *
 * Code syntax: [d0].d1d2d3...  Each digit is hex (0-9, a-f).  Bit j of digit d_k
 * means: you may remove k beans from a heap and leave exactly j nonempty heaps
 * (j = 0..3; j = 3 needs a hexadecimal digit >= 8).  d0 (before the point) allows
 * splitting a heap without removing anything (e.g. "4." = split any heap).
 *
 * Output directory contains:
 *   summary.txt     rule, timing, period (proved via the octal periodicity theorem),
 *                   records, value frequencies, sparse-space statistics
 *   values.csv      n,G(n)
 *   values.png      G(n) against n (density-shaded; red = new record value)
 *   wrap.png        sequence wrapped in rows of width W, coloured by value
 *   wrapdiff.png    same layout; dark cell where G(n) != G(n-W)
 *   autocorr.png    fraction of n (second half) with G(n) = G(n+p), p = 1..L
 *   histogram.png   frequency of each value (log scale), bars in palette colours
 *   index.html      gallery + interactive wrap explorer (open in a browser)
 *
 * Algorithm: the default is a tight brute-force mex over all options
 * (O(n) per heap size for two-heap moves).  `--sparse` enables an experimental
 * rare/common variant: values are split by the parity of popcount(v & mask);
 * a rare XOR needs exactly one rare heap, so rare reachable values come from
 * the few rare heaps, and the pair scan stops once the smallest missing value
 * is rare.  Proving that a *common* value is unreachable still needs a full
 * scan, so on .6 and .007 it is not faster.  `--check` compares both methods.
 *
 * Build with -DOCTAL_NO_MAIN -shared -fPIC for the library used by the Python
 * package (functions octal_rule_name, octal_extend, octal_period; see octal.h).
 *
 * Sections: rules · Grundy computation · periodicity · graphics (PNG) ·
 * output (HTML) · library API · command-line program.
 */
#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <zlib.h>

#include "octal.h"

/** Maximum number of code digits after the point (d_1 .. d_MAXDIG). */
#define MAXDIG 64

/** Named rules that are not plain take-and-break codes.
 *  SP_GRUNDY: Grundy's game (split into two *unequal* heaps).
 *  SP_COUPLES: Couples-are-forever (split any heap except a heap of 2). */
enum { SP_NONE, SP_GRUNDY, SP_COUPLES };

/**
 * @brief A parsed game rule.
 *
 * Bit j of d[k] means: removing k beans and leaving exactly j nonempty heaps is
 * allowed (j = 0..3).  d[0] describes splitting without removal.
 */
typedef struct {
    unsigned d[MAXDIG + 1]; /**< code digits d[0] .. d[t]; zero beyond t */
    int t;                  /**< index of the last nonzero digit (0 if only d[0]) */
    int special;            /**< SP_NONE, SP_GRUNDY or SP_COUPLES */
    int hex;                /**< some digit has bit 3 (splitting into 3 heaps) */
    char code[80];          /**< the code as given by the user */
    char name[120];         /**< human-readable name, e.g. "octal game .007" */
} Rule;

/** @brief Result of compute(): the nim-sequence plus statistics. */
typedef struct {
    uint16_t *G;         /**< G[0..N): nim-values (owned; free() it) */
    long N;              /**< number of heap sizes computed */
    double secs;         /**< wall-clock seconds */
    int sparse;          /**< sparse method was active at the end */
    unsigned mask;       /**< rare/common mask (valid if sparse) */
    long nrare;          /**< number of rare heap sizes (valid if sparse) */
    long long pairs;     /**< two-heap pairs examined (work counter) */
} Result;

/** @brief Eventual period found by find_period(). */
typedef struct {
    long pre, per;       /**< preperiod i and period p: G(n+p) = G(n) for n >= i */
    int found, proven;   /**< found: a period was detected; proven: certified by the theorem */
} Period;

/** Suppress progress and summary output (`-q`).  Command-line program only; set
 *  once before any computation and never written by the library functions. */
static int quiet = 0;

/** Print "octal: msg" to stderr and exit with status 1. */
static void die(const char *msg) {
    fprintf(stderr, "octal: %s\n", msg);
    exit(1);
}

/** malloc() that never returns NULL (dies instead); n = 0 allocates 1 byte. */
static void *xmalloc(size_t n) {
    void *p = malloc(n ? n : 1);
    if (!p) die("out of memory");
    return p;
}

/** calloc() that never returns NULL (dies instead). */
static void *xcalloc(size_t n, size_t s) {
    void *p = calloc(n ? n : 1, s ? s : 1);
    if (!p) die("out of memory");
    return p;
}

/** Monotonic clock in seconds, for timing. */
static double now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

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
static int parse_rule(const char *s, Rule *R) {
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

/**
 * @brief Compute G(0) .. G(N-1) (command-line program).
 *
 * Uses grundy_brute(), or grundy_sparse() when @p allow_sparse is set and a
 * mask makes fewer than 25% of heap sizes rare.  The mask is re-chosen at
 * n = 2048, 8192, 32768, ...  Shows progress on stderr when it is a terminal.
 * Dies if a value exceeds 65535 or memory runs out (command-line use only; the
 * library uses octal_extend(), which returns error codes instead).
 *
 * @param R             rule
 * @param N             number of heap sizes
 * @param allow_sparse  enable the experimental sparse method
 * @return the sequence and statistics; the caller frees Result.G
 */
static Result compute(const Rule *R, long N, int allow_sparse) {
    Result res = {0};
    res.N = N;
    res.G = xcalloc((size_t)N, sizeof *res.G);
    uint16_t *G = res.G;
    unsigned maxG = 0, vcap = 2;
    Scratch S = {0};
    if (scratch_reserve(&S, 2 * vcap + 2)) die("out of memory");
    long *rare = NULL, nrare = 0, rare_cap = 0;
    int sparse = 0;
    unsigned mask = 0;
    long next_check = 2048;
    double t0 = now(), last = t0;
    int show = !quiet && isatty(2);

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
 * @brief Find an eventual period of G[0..N).
 *
 * For p = 1, 2, ..., N/2 the preperiod i is the smallest index with
 * G(n) = G(n+p) for all i <= n < N-p (found by scanning backwards).
 *
 * Octal periodicity theorem (Winning Ways, ch. 4): if G(n+p) = G(n) for all
 * i <= n < 2i + p + t, where t is the last nonzero code digit, the sequence is
 * periodic from i on.  So p is *proved* once N >= 2i + 2p + t; this is only
 * claimed for plain octal codes (no d0, no hex digits, no named rules).
 *
 * Returns the first proved period; otherwise the smallest p with N - i >= 3p
 * as an *apparent* period (multiples of it are skipped, keeping the search
 * linear for periodic data); otherwise found = 0.
 */
static Period find_period(const uint16_t *G, long N, const Rule *R) {
    int can_prove = R->special == SP_NONE && R->d[0] == 0 && !R->hex;
    Period ap = {0};
    for (long p = 1; p <= N / 2; p++) {
        if (ap.found && p % ap.per == 0) continue;
        long n = N - p - 1;
        while (n >= 0 && G[n] == G[n + p]) n--;
        long i = n + 1;
        if (can_prove && (long long)N >= 2LL * i + 2LL * p + R->t) {
            Period r = {i, p, 1, 1};
            return r;
        }
        if (!ap.found && N - i >= 3 * p) {
            ap.pre = i; ap.per = p; ap.found = 1;
        }
    }
    return ap;
}

/* -------------------------------------------------------------- graphics */

/** @brief An RGB image in memory (8 bits per channel, rows top to bottom). */
typedef struct {
    int w, h;            /**< width and height in pixels */
    uint8_t *p;          /**< w*h*3 bytes, row-major RGB (owned; free() it) */
} Image;

/** New @p w x @p h image filled with colour (r, g, b). */
static Image img_new(int w, int h, int r, int g, int b) {
    Image im = {w, h, xmalloc((size_t)w * h * 3)};
    for (long i = 0; i < (long)w * h; i++) {
        im.p[3 * i] = r; im.p[3 * i + 1] = g; im.p[3 * i + 2] = b;
    }
    return im;
}

/** Set pixel (x, y) to colour @p c; coordinates outside the image are ignored. */
static inline void px(Image *im, int x, int y, const uint8_t c[3]) {
    if (x < 0 || y < 0 || x >= im->w || y >= im->h) return;
    uint8_t *q = im->p + 3 * ((size_t)y * im->w + x);
    q[0] = c[0]; q[1] = c[1]; q[2] = c[2];
}

/** Fill the rectangle [x0, x1) x [y0, y1) with colour @p c (clipped). */
static void rect(Image *im, int x0, int y0, int x1, int y1, const uint8_t c[3]) {
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) px(im, x, y, c);
}

/** Store @p v big-endian in b[0..3] (PNG byte order). */
static void put32(uint8_t *b, uint32_t v) {
    b[0] = v >> 24; b[1] = v >> 16; b[2] = v >> 8; b[3] = v;
}

/** Write one PNG chunk: length, 4-byte @p type, @p data, CRC-32 of type+data. */
static void chunk(FILE *f, const char *type, const uint8_t *data, uint32_t len) {
    uint8_t h[8];
    put32(h, len);
    memcpy(h + 4, type, 4);
    fwrite(h, 1, 8, f);
    if (len) fwrite(data, 1, len, f);
    uLong crc = crc32(0, (const Bytef *)type, 4);
    if (len) crc = crc32(crc, data, len);
    uint8_t c[4];
    put32(c, (uint32_t)crc);
    fwrite(c, 1, 4, f);
}

/**
 * @brief Write @p im as an 8-bit RGB PNG file.
 *
 * Every row uses filter type 0; the image data is compressed with zlib level 6.
 * Dies on I/O or compression errors.
 */
static void png_save(const Image *im, const char *path) {
    size_t stride = (size_t)im->w * 3 + 1, rawlen = stride * im->h;
    uint8_t *raw = xmalloc(rawlen);
    for (int y = 0; y < im->h; y++) {
        raw[y * stride] = 0;
        memcpy(raw + y * stride + 1, im->p + (size_t)y * im->w * 3, (size_t)im->w * 3);
    }
    uLongf zlen = compressBound(rawlen);
    uint8_t *z = xmalloc(zlen);
    if (compress2(z, &zlen, raw, rawlen, 6) != Z_OK) die("zlib compression failed");
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); exit(1); }
    fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
    uint8_t ihdr[13];
    put32(ihdr, im->w);
    put32(ihdr + 4, im->h);
    ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = ihdr[11] = ihdr[12] = 0;
    chunk(f, "IHDR", ihdr, 13);
    chunk(f, "IDAT", z, (uint32_t)zlen);
    chunk(f, "IEND", NULL, 0);
    fclose(f);
    free(raw);
    free(z);
}

/** Convert HSV (all components in [0, 1]) to 8-bit RGB in @p c. */
static void hsv(double h, double s, double v, uint8_t c[3]) {
    double r, g, b, f = h * 6 - floor(h * 6);
    double p = v * (1 - s), q = v * (1 - f * s), t = v * (1 - (1 - f) * s);
    switch ((int)floor(h * 6) % 6) {
    case 0: r = v; g = t; b = p; break;
    case 1: r = q; g = v; b = p; break;
    case 2: r = p; g = v; b = t; break;
    case 3: r = p; g = q; b = v; break;
    case 4: r = t; g = p; b = v; break;
    default: r = v; g = p; b = q; break;
    }
    c[0] = (uint8_t)(255 * r + .5); c[1] = (uint8_t)(255 * g + .5); c[2] = (uint8_t)(255 * b + .5);
}

/**
 * @brief Colour of nim-value @p v, shared by all plots and the HTML explorer.
 *
 * 0 (P-positions) is near-black (25, 25, 35).  v > 0 has hue v * 0.618034 mod 1
 * (golden-ratio steps keep neighbouring values distinct), saturation 0.70 and
 * value 0.92.  The Python package (octalgames.palette) uses the same formula.
 */
static void palette(unsigned v, uint8_t c[3]) {
    if (v == 0) { c[0] = 25; c[1] = 25; c[2] = 35; return; }
    hsv(fmod(v * 0.6180339887, 1.0), 0.70, 0.92, c);
}

/** Plot colours: grid lines, record markers, separator line in wrap plots. */
static const uint8_t GRID[3] = {225, 225, 232},
                     RED[3] = {215, 40, 40}, SEP[3] = {200, 30, 30};

/**
 * @brief values.png: G(n) against n, 1600 x 640 pixels.
 *
 * Points are binned per pixel and shaded by log(count); grey lines mark powers
 * of two; red squares mark each new record value.
 *
 * @param G     nim-values
 * @param N     number of values
 * @param maxG  largest value (top of the y axis)
 * @param path  output file
 */
static void plot_values(const uint16_t *G, long N, unsigned maxG, const char *path) {
    const int W = 1600, H = 640;
    Image im = img_new(W, H, 255, 255, 255);
    unsigned top = maxG ? maxG : 1;
    for (unsigned v = 1; v <= top; v *= 2) { /* gridlines at powers of two */
        int y = H - 1 - (int)((double)v * (H - 1) / top);
        rect(&im, 0, y, W, y + 1, GRID);
    }
    uint32_t *cnt = xcalloc((size_t)W * H, sizeof *cnt);
    uint32_t cmax = 1;
    for (long n = 0; n < N; n++) {
        int x = (int)((double)n * W / N);
        int y = H - 1 - (int)((double)G[n] * (H - 1) / top);
        uint32_t c = ++cnt[(size_t)y * W + x];
        if (c > cmax) cmax = c;
    }
    int dot = N < W ? (int)(W / N) : 1;
    if (dot > 6) dot = 6;
    if (dot < 1) dot = 1;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            uint32_t c = cnt[(size_t)y * W + x];
            if (!c) continue;
            double t = 0.35 + 0.65 * log1p(c) / log1p(cmax);
            uint8_t col[3] = {(uint8_t)(230 - 210 * t), (uint8_t)(235 - 190 * t), (uint8_t)(250 - 110 * t)};
            rect(&im, x, y - dot / 2, x + dot, y - dot / 2 + dot, col);
        }
    unsigned rec = 0;
    for (long n = 0; n < N; n++)
        if (G[n] > rec || n == 0) {
            rec = G[n];
            int x = (int)((double)n * W / N);
            int y = H - 1 - (int)((double)G[n] * (H - 1) / top);
            rect(&im, x - 2, y - 2, x + 3, y + 3, RED);
        }
    png_save(&im, path);
    free(cnt);
    free(im.p);
}

/**
 * @brief Layout shared by wrap.png and wrapdiff.png.
 *
 * Rows of W values; if there are too many rows for about 1800 pixels, the first
 * rows_top and the last rows_bot rows are shown, separated by a red line.
 */
typedef struct {
    int W;               /**< values per row */
    int cs;              /**< cell size in pixels (1..12) */
    int rows_top;        /**< rows drawn from the start */
    int rows_bot;        /**< rows drawn from the end (0 if everything fits) */
    int sep;             /**< height of the separator line in pixels (0 or 4) */
    long rows;           /**< total number of rows, ceil(N / W) */
} WrapLayout;

/** Compute the wrap layout for @p N values in rows of @p W (cell size 1600/W, clamped to 1..12). */
static WrapLayout wrap_layout(long N, int W) {
    WrapLayout L;
    L.W = W;
    L.cs = 1600 / W;
    if (L.cs < 1) L.cs = 1;
    if (L.cs > 12) L.cs = 12;
    L.rows = (N + W - 1) / W;
    long maxrows = 1800 / L.cs;
    if (L.rows <= maxrows) {
        L.rows_top = (int)L.rows; L.rows_bot = 0; L.sep = 0;
    } else {
        L.rows_top = (int)(maxrows / 2); L.rows_bot = (int)(maxrows / 2); L.sep = 4;
    }
    return L;
}

/**
 * @brief wrap.png / wrapdiff.png: the sequence in rows of @p W values.
 *
 * @param G     nim-values
 * @param N     number of values
 * @param W     row width
 * @param diff  0: colour cells by value (palette());
 *              1: dark red where G(n) != G(n-W), light otherwise, grey in the first row
 * @param path  output file
 */
static void plot_wrap(const uint16_t *G, long N, int W, int diff, const char *path) {
    WrapLayout L = wrap_layout(N, W);
    int iw = W * L.cs, ih = (L.rows_top + L.rows_bot) * L.cs + L.sep;
    Image im = img_new(iw, ih, 255, 255, 255);
    for (int part = 0; part < 2; part++) {
        int nrows = part ? L.rows_bot : L.rows_top;
        long r0 = part ? L.rows - L.rows_bot : 0;
        int y0 = part ? L.rows_top * L.cs + L.sep : 0;
        for (int r = 0; r < nrows; r++)
            for (int c = 0; c < W; c++) {
                long n = (r0 + r) * W + c;
                if (n >= N) continue;
                uint8_t col[3];
                if (!diff) palette(G[n], col);
                else if (n < W) { col[0] = col[1] = col[2] = 200; }
                else if (G[n] == G[n - W]) { col[0] = 248; col[1] = 248; col[2] = 245; }
                else { col[0] = 150; col[1] = 20; col[2] = 30; }
                rect(&im, c * L.cs, y0 + r * L.cs, (c + 1) * L.cs, y0 + (r + 1) * L.cs, col);
            }
    }
    if (L.sep) rect(&im, 0, L.rows_top * L.cs, iw, L.rows_top * L.cs + L.sep, SEP);
    png_save(&im, path);
    free(im.p);
}

/**
 * @brief Autocorrelation of the second half of the sequence.
 *
 * @return newly allocated array a[0..L]; a[p] = fraction of n in [N/2, N-p)
 *         with G(n) = G(n+p); a[0] = 0 (unused).  The caller frees it.
 */
static double *autocorr(const uint16_t *G, long N, int L) {
    double *a = xcalloc(L + 1, sizeof *a);
    long start = N / 2;
    for (int p = 1; p <= L; p++) {
        long hit = 0, tot = 0;
        for (long n = start; n + p < N; n++, tot++) hit += G[n] == G[n + p];
        a[p] = tot ? (double)hit / tot : 0;
    }
    return a;
}

/**
 * @brief autocorr.png: bars of a[1..L] (several lags per pixel use the maximum).
 *
 * @param a     autocorrelation from autocorr()
 * @param L     number of lags
 * @param per   detected period (multiples drawn orange), or 0
 * @param peak  best lag (drawn red)
 * @param path  output file
 */
static void plot_autocorr(const double *a, int L, int per, int peak, const char *path) {
    int W = L * 2 < 1600 ? (L * 2 < 400 ? 400 : L * 2) : 1600, H = 360;
    Image im = img_new(W, H, 255, 255, 255);
    for (int q = 1; q <= 3; q++) rect(&im, 0, H - H * q / 4, W, H - H * q / 4 + 1, GRID);
    for (int x = 0; x < W; x++) {
        int p0 = 1 + (int)((long)x * L / W), p1 = 1 + (int)((long)(x + 1) * L / W);
        if (p1 <= p0) p1 = p0 + 1;
        double v = 0;
        int best = p0;
        for (int p = p0; p < p1 && p <= L; p++)
            if (a[p] > v) { v = a[p]; best = p; }
        uint8_t col[3] = {120, 130, 150};
        if (per && best % per == 0) { col[0] = 235; col[1] = 140; col[2] = 30; }
        if (best == peak) { col[0] = RED[0]; col[1] = RED[1]; col[2] = RED[2]; }
        rect(&im, x, H - (int)(v * (H - 1)), x + 1, H, col);
    }
    png_save(&im, path);
    free(im.p);
}

/**
 * @brief histogram.png: frequency of each value 0..maxG, log10(count+1) scale.
 *
 * Bars use palette() colours, so the histogram doubles as a colour legend.
 */
static void plot_hist(const long *cnt, unsigned maxG, const char *path) {
    int nv = (int)maxG + 1;
    int bw = 1600 / nv;
    if (bw < 1) bw = 1;
    if (bw > 24) bw = 24;
    int W = nv * bw < 1600 ? nv * bw : 1600, H = 320;
    if (W < 200) W = 200;
    Image im = img_new(W, H, 255, 255, 255);
    double lmax = 1;
    for (int v = 0; v < nv; v++)
        if (log10(cnt[v] + 1.0) > lmax) lmax = log10(cnt[v] + 1.0);
    for (int q = 1; q < (int)lmax + 1; q++) {
        int y = H - (int)(q / lmax * (H - 1));
        if (y > 0) rect(&im, 0, y, W, y + 1, GRID);
    }
    for (int v = 0; v < nv; v++) {
        if (!cnt[v]) continue;
        int x0 = (int)((long)v * W / nv), x1 = (int)((long)(v + 1) * W / nv);
        if (x1 <= x0) x1 = x0 + 1;
        int h = (int)(log10(cnt[v] + 1.0) / lmax * (H - 1));
        if (h < 1) h = 1;
        uint8_t col[3];
        palette(v, col);
        rect(&im, x0, H - h, x1 - (bw > 3), H, col);
    }
    png_save(&im, path);
    free(im.p);
}

/* ---------------------------------------------------------------- output */

/** out = dir + "/" + name (truncated to @p sz bytes). */
static void join(char *out, size_t sz, const char *dir, const char *name) {
    snprintf(out, sz, "%s/%s", dir, name);
}

/**
 * @brief Write DIR/index.html: summary, plots, records and the interactive explorer.
 *
 * The first @p embed values are embedded as a JavaScript Uint16Array, so the page
 * works from file:// without a server.  The explorer's JavaScript uses the same
 * palette as palette().
 *
 * @param dir    output directory
 * @param R      rule
 * @param res    computed sequence
 * @param P      period result
 * @param maxG   largest value
 * @param maxat  first n with G(n) = maxG
 * @param wrapW  default wrap width
 * @param peak   best autocorrelation lag
 * @param L      number of autocorrelation lags
 * @param embed  maximum number of values to embed
 * @param recn   heap sizes of the record values
 * @param recv   the record values
 * @param nrec   number of records
 */
static void write_html(const char *dir, const Rule *R, const Result *res, const Period *P,
                       unsigned maxG, long maxat, int wrapW, int peak, int L, long embed,
                       const long *recn, const unsigned *recv, int nrec) {
    char path[4096];
    join(path, sizeof path, dir, "index.html");
    FILE *f = fopen(path, "w");
    if (!f) { perror(path); exit(1); }
    long M = res->N < embed ? res->N : embed;
    char per[256];
    if (P->found)
        snprintf(per, sizeof per, "%s period %ld, preperiod %ld", P->proven ? "Proved:" : "Apparent (unproved):",
                 P->per, P->pre);
    else
        snprintf(per, sizeof per, "No period found up to n = %ld", res->N);

    fprintf(f,
"<!doctype html>\n<html lang=\"en\"><head><meta charset=\"utf-8\">\n"
"<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">\n"
"<title>%s — nim-sequence explorer</title>\n"
"<style>\n"
":root{--bg:#fafafa;--fg:#1b1d22;--mut:#666b75;--card:#fff;--bd:#dfe1e6;--acc:#2f5fd0}\n"
"@media (prefers-color-scheme:dark){:root{--bg:#15171b;--fg:#e6e7ea;--mut:#9aa0aa;--card:#1d2026;--bd:#30343c;--acc:#7aa2ff}}\n"
"body{margin:0;background:var(--bg);color:var(--fg);font:15px/1.5 system-ui,sans-serif}\n"
"main{max-width:1680px;margin:0 auto;padding:16px}\n"
"h1{font-size:22px;margin:8px 0 4px}h2{font-size:17px;margin:28px 0 6px}\n"
".mut{color:var(--mut)}.card{background:var(--card);border:1px solid var(--bd);border-radius:8px;padding:12px;margin:10px 0;overflow:auto}\n"
"img{max-width:100%%;height:auto;image-rendering:pixelated;border:1px solid var(--bd);background:#fff}\n"
"table{border-collapse:collapse}td,th{padding:3px 12px 3px 0;text-align:left;vertical-align:top}\n"
".ctl{display:flex;flex-wrap:wrap;gap:10px 18px;align-items:center;margin-bottom:10px}\n"
".ctl label{display:flex;gap:6px;align-items:center}input[type=number]{width:90px}\n"
"button,select,input{font:inherit}#cvwrap{overflow:auto;max-height:80vh;border:1px solid var(--bd)}\n"
"#cv{image-rendering:pixelated;display:block;background:#fff}#info{font-family:ui-monospace,monospace;min-height:1.5em}\n"
"code{font-family:ui-monospace,monospace}\n"
"</style></head><body><main>\n"
"<h1>%s</h1>\n<div class=\"mut\">%s</div>\n"
"<div class=\"card\"><table>\n"
"<tr><th>Heap sizes</th><td>0 … %ld</td></tr>\n"
"<tr><th>Periodicity</th><td>%s</td></tr>\n"
"<tr><th>Largest value</th><td>%u (first at n = %ld)</td></tr>\n"
"<tr><th>Computation</th><td>%.2f s, %s method%s</td></tr>\n"
"</table></div>\n",
            R->code, R->code, R->name, res->N - 1, per, maxG, maxat, res->secs,
            res->sparse ? "sparse-space" : "brute-force", "");
    if (res->sparse)
        fprintf(f, "<p class=\"mut\">Sparse space: mask 0x%x makes %ld heap sizes (%.3f%%) rare. "
                   "Choose <em>rare</em> in the explorer to see them.</p>\n",
                res->mask, res->nrare, 100.0 * res->nrare / res->N);

    fprintf(f,
"<h2>Interactive wrap explorer</h2>\n"
"<p class=\"mut\">Row r, column c shows n = start + r·W + c. When W is a period (or a multiple of one), "
"the picture turns into vertical stripes, and in <em>diff</em> mode it turns blank. Arrow keys ←/→ change W. "
"Embedded data: first %ld values.</p>\n"
"<div class=\"card\"><div class=\"ctl\">\n"
"<label>Width W <input id=\"w\" type=\"number\" min=\"1\" max=\"5000\" value=\"%d\"></label>\n"
"<button id=\"wm\">−1</button><button id=\"wp\">+1</button><button id=\"wd\">×2</button><button id=\"wh\">÷2</button>\n"
"%s"
"<label>Cell <input id=\"cs\" type=\"number\" min=\"1\" max=\"24\" value=\"4\"> px</label>\n"
"<label>Start n <input id=\"st\" type=\"number\" min=\"0\" value=\"0\" step=\"1\"></label>\n"
"<label>Mode <select id=\"mode\"><option value=\"val\">values</option><option value=\"diff\">diff: G(n) ≠ G(n−W)</option>"
"<option value=\"zero\">P-positions (G = 0)</option><option value=\"bit\">bit plane</option>"
"<option value=\"rare\">rare values</option><option value=\"rec\">≥ threshold</option></select></label>\n"
"<label>Bit / threshold <input id=\"bit\" type=\"number\" min=\"0\" value=\"0\"></label>\n"
"</div><div id=\"info\" class=\"mut\">hover over the picture</div><div id=\"cvwrap\"><canvas id=\"cv\"></canvas></div></div>\n",
            M, wrapW,
            P->found ? "<button id=\"wper\">use period</button>\n" : "");

    fprintf(f,
"<h2>Values</h2><div class=\"card\"><img src=\"values.png\" alt=\"G(n) against n\">\n"
"<p class=\"mut\">x: n from 0 to %ld. y: G(n) from 0 (bottom) to %u (top), with grey lines at powers of two. "
"Darker means more points in that pixel. Red squares mark new record values.</p></div>\n"
"<h2>Wrapped at W = %d</h2><div class=\"card\"><img src=\"wrap.png\" alt=\"wrapped sequence\">\n"
"<p class=\"mut\">Each row holds W consecutive values, coloured by value (0 is near-black; colours match the histogram). "
"If the sequence is long, the first and last rows are shown either side of a red line.</p>\n"
"<img src=\"wrapdiff.png\" alt=\"difference with previous row\">\n"
"<p class=\"mut\">Dark red: G(n) ≠ G(n−W). Once the preperiod ends, a period W leaves only light cells.</p></div>\n"
"<h2>Autocorrelation</h2><div class=\"card\"><img src=\"autocorr.png\" alt=\"autocorrelation\">\n"
"<p class=\"mut\">For lag p = 1 … %d (left to right), bar height is the fraction of n in the second half with G(n) = G(n+p). "
"The best lag is red (p = %d)%s.</p></div>\n"
"<h2>Value frequencies</h2><div class=\"card\"><img src=\"histogram.png\" alt=\"histogram\">\n"
"<p class=\"mut\">Values 0 … %u, left to right. Height is log₁₀(count+1). Short bars are rare values, which the sparse-space method exploits.</p></div>\n",
            res->N - 1, maxG, wrapW, L, peak, P->found ? "; multiples of the period are orange" : "", maxG);

    fprintf(f, "<h2>Record values</h2><div class=\"card\"><table><tr><th>n</th><th>G(n)</th></tr>\n");
    for (int i = 0; i < nrec; i++) fprintf(f, "<tr><td>%ld</td><td>%u</td></tr>\n", recn[i], recv[i]);
    fprintf(f, "</table></div>\n");

    fprintf(f, "<script>\nconst G=new Uint16Array([");
    for (long n = 0; n < M; n++) fprintf(f, "%s%u", n ? (n % 40 ? "," : ",\n") : "", res->G[n]);
    fprintf(f, "]);\nconst PER=%ld, MASK=%u;\n", P->found ? P->per : 0L, res->sparse ? res->mask : 0u);
    fputs(
"function hsv(h,s,v){const i=Math.floor(h*6),f=h*6-i,p=v*(1-s),q=v*(1-f*s),t=v*(1-(1-f)*s);\n"
" const m=[[v,t,p],[q,v,p],[p,v,t],[p,q,v],[t,p,v],[v,p,q]][i%6];return m.map(x=>Math.round(255*x));}\n"
"const PAL=new Map();function col(v){if(v===0)return[25,25,35];let c=PAL.get(v);if(!c){c=hsv((v*0.6180339887)%1,0.70,0.92);PAL.set(v,c);}return c;}\n"
"function par(x){x^=x>>8;x^=x>>4;x^=x>>2;x^=x>>1;return x&1;}\n"
"const $=id=>document.getElementById(id),cv=$('cv'),ctx=cv.getContext('2d');\n"
"let cs=4,W=1,start=0;\n"
"function draw(){W=Math.max(1,+$('w').value|0);cs=Math.max(1,+$('cs').value|0);if(W*cs>6000)cs=Math.max(1,Math.floor(6000/W));\n"
" start=Math.max(0,+$('st').value|0);const mode=$('mode').value,b=+$('bit').value|0;\n"
" const rows=Math.max(1,Math.min(Math.ceil((G.length-start)/W),Math.floor(3000/cs)));\n"
" cv.width=W*cs;cv.height=rows*cs;const img=ctx.createImageData(cv.width,cv.height),d=img.data;\n"
" for(let r=0;r<rows;r++)for(let c=0;c<W;c++){const n=start+r*W+c;let k;\n"
"  if(n>=G.length)k=[255,255,255];else{const g=G[n];\n"
"   if(mode==='val')k=col(g);\n"
"   else if(mode==='diff')k=n<W?[200,200,200]:(G[n-W]===g?[248,248,245]:[150,20,30]);\n"
"   else if(mode==='zero')k=g===0?[20,20,20]:[245,245,245];\n"
"   else if(mode==='bit')k=(g>>b)&1?[20,20,20]:[245,245,245];\n"
"   else if(mode==='rare')k=MASK&&par(g&MASK)?[200,30,30]:[240,240,240];\n"
"   else k=g>=b?col(g):[245,245,245];}\n"
"  for(let y=0;y<cs;y++){let o=((r*cs+y)*cv.width+c*cs)*4;for(let x=0;x<cs;x++,o+=4){d[o]=k[0];d[o+1]=k[1];d[o+2]=k[2];d[o+3]=255;}}}\n"
" ctx.putImageData(img,0,0);}\n"
"cv.addEventListener('mousemove',e=>{const R=cv.getBoundingClientRect(),x=(e.clientX-R.left)*cv.width/R.width,y=(e.clientY-R.top)*cv.height/R.height;\n"
" const n=start+Math.floor(y/cs)*W+Math.floor(x/cs);$('info').textContent=n<G.length?`n = ${n}   G(n) = ${G[n]}   row ${Math.floor(y/cs)}, column ${Math.floor(x/cs)}`:'';});\n"
"for(const id of['w','cs','st','mode','bit'])$(id).addEventListener('input',draw);\n"
"const setW=v=>{$('w').value=Math.max(1,v);draw();};\n"
"$('wm').onclick=()=>setW(W-1);$('wp').onclick=()=>setW(W+1);$('wd').onclick=()=>setW(W*2);$('wh').onclick=()=>setW(Math.floor(W/2));\n"
"if($('wper'))$('wper').onclick=()=>setW(PER);\n"
"document.addEventListener('keydown',e=>{if(e.target.tagName==='INPUT'||e.target.tagName==='SELECT')return;\n"
" if(e.key==='ArrowLeft')setW(W-1);if(e.key==='ArrowRight')setW(W+1);});\n"
"draw();\n</script>\n</main></body></html>\n", f);
    fclose(f);
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

/** See octal.h.  Wraps find_period(): returns 0 none, 1 apparent, 2 proved
 *  (-1 bad code) and stores out[0] = preperiod, out[1] = period. */
int octal_period(const char *code, const uint16_t *G, long N, long *out) {
    Rule R;
    if (parse_rule(code, &R)) return -1;
    Period P = find_period(G, N, &R);
    out[0] = P.pre;
    out[1] = P.per;
    return P.found ? (P.proven ? 2 : 1) : 0;
}

#ifndef OCTAL_NO_MAIN
/** Print command-line help to stderr and exit with status 2. */
static void usage(void) {
    fprintf(stderr,
"usage: octal CODE [options]\n"
"  CODE        octal/hex code such as .007  0.6  .137  4.  .f6, or 'grundy' / 'couples'\n"
"  -n N        number of heap sizes to compute (default 10000)\n"
"  -o DIR      output directory (default out_CODE)\n"
"  -w W        wrap width for wrap.png (default: period, else best autocorrelation lag)\n"
"  -L LAGS     maximum autocorrelation lag (default 1000)\n"
"  -e M        values embedded in index.html (default 200000)\n"
"  --sparse    use the sparse-space method (experimental; brute force is the default\n"
"              and is usually as fast or faster)\n"
"  --check     compute with both methods and compare (for testing)\n"
"  -p          print the sequence to stdout and skip output files\n"
"  -q          quiet\n");
    exit(2);
}

/**
 * @brief Command-line program: compute, analyse and write all output files.
 *
 * Steps: parse options and the code; compute(); optionally `--check` against
 * brute force or `-p` print; statistics (max value, histogram, records);
 * find_period(); autocorrelation and best lag; choose the wrap width; write
 * values.csv, the PNG plots, index.html and summary.txt.  See usage().
 */
int main(int argc, char **argv) {
    long N = 10000, embed = 200000;
    int W = 0, L = 1000, sparse = 0, check = 0, print = 0;
    const char *dir = NULL;
    static struct option lo[] = {{"brute", no_argument, 0, 'b'}, {"sparse", no_argument, 0, 's'},
                                 {"check", no_argument, 0, 'c'},
                                 {"help", no_argument, 0, 'h'}, {0, 0, 0, 0}};
    int c;
    while ((c = getopt_long(argc, argv, "n:o:w:L:e:pqh", lo, NULL)) != -1) {
        switch (c) {
        case 'n': N = atol(optarg); break;
        case 'o': dir = optarg; break;
        case 'w': W = atoi(optarg); break;
        case 'L': L = atoi(optarg); break;
        case 'e': embed = atol(optarg); break;
        case 'b': sparse = 0; break;
        case 's': sparse = 1; break;
        case 'c': check = 1; break;
        case 'p': print = 1; break;
        case 'q': quiet = 1; break;
        default: usage();
        }
    }
    if (optind != argc - 1) usage();
    if (N < 2 || N > 2000000000L) die("N must be between 2 and 2e9");
    Rule R;
    if (parse_rule(argv[optind], &R)) die("cannot parse code (examples: .007  0.6  .137  4.  grundy)");

    Result res = compute(&R, N, sparse || check);

    if (check) {
        Result b = compute(&R, N, 0);
        for (long n = 0; n < N; n++)
            if (b.G[n] != res.G[n]) {
                printf("MISMATCH at n = %ld: sparse %u, brute %u\n", n, res.G[n], b.G[n]);
                return 1;
            }
        printf("check OK: %s, n < %ld, sparse %.2f s (%s), brute %.2f s\n", R.code, N, res.secs,
               res.sparse ? "active" : "inactive", b.secs);
        return 0;
    }

    if (print) {
        for (long n = 0; n < N; n++) printf("%u%c", res.G[n], (n % 30 == 29 || n == N - 1) ? '\n' : ' ');
        return 0;
    }

    /* statistics */
    unsigned maxG = 0;
    long maxat = 0;
    for (long n = 0; n < N; n++)
        if (res.G[n] > maxG) { maxG = res.G[n]; maxat = n; }
    long *cnt = xcalloc(maxG + 1, sizeof *cnt);
    for (long n = 0; n < N; n++) cnt[res.G[n]]++;
    int nrec = 0;
    long *recn = xmalloc(sizeof(long) * (maxG + 2));
    unsigned *recv = xmalloc(sizeof(unsigned) * (maxG + 2));
    for (long n = 0, rec = -1; n < N; n++)
        if ((long)res.G[n] > rec) { rec = res.G[n]; recn[nrec] = n; recv[nrec++] = res.G[n]; }

    Period P = find_period(res.G, N, &R);
    if (L > N / 3) L = (int)(N / 3);
    if (L < 1) L = 1;
    double *ac = autocorr(res.G, N, L);
    int peak = 1;
    double amax = 0;
    for (int p = 1; p <= L; p++) if (ac[p] > amax) amax = ac[p];
    for (int p = 1; p <= L; p++) if (ac[p] >= amax - 1e-9) { peak = p; break; }
    if (W <= 0) W = P.found && P.per <= 5000 ? (int)P.per : peak;
    if (W > 5000) W = 5000;

    char defdir[200];
    if (!dir) {
        snprintf(defdir, sizeof defdir, "out_%s", R.code);
        dir = defdir;
    }
    if (mkdir(dir, 0755) && errno != EEXIST) { perror(dir); return 1; }

    char path[4096];
    join(path, sizeof path, dir, "values.csv");
    FILE *f = fopen(path, "w");
    if (!f) { perror(path); return 1; }
    fprintf(f, "n,G\n");
    for (long n = 0; n < N; n++) fprintf(f, "%ld,%u\n", n, res.G[n]);
    fclose(f);

    join(path, sizeof path, dir, "values.png");    plot_values(res.G, N, maxG, path);
    join(path, sizeof path, dir, "wrap.png");      plot_wrap(res.G, N, W, 0, path);
    join(path, sizeof path, dir, "wrapdiff.png");  plot_wrap(res.G, N, W, 1, path);
    join(path, sizeof path, dir, "autocorr.png");  plot_autocorr(ac, L, P.found ? (int)P.per : 0, peak, path);
    join(path, sizeof path, dir, "histogram.png"); plot_hist(cnt, maxG, path);
    write_html(dir, &R, &res, &P, maxG, maxat, W, peak, L, embed, recn, recv, nrec);

    /* summary */
    join(path, sizeof path, dir, "summary.txt");
    FILE *s = fopen(path, "w");
    if (!s) { perror(path); return 1; }
    FILE *outs[2] = {s, quiet ? NULL : stdout};
    for (int o = 0; o < 2; o++) {
        FILE *g = outs[o];
        if (!g) continue;
        fprintf(g, "%s\n", R.name);
        fprintf(g, "heap sizes      0 .. %ld\n", N - 1);
        fprintf(g, "time            %.2f s (%s)\n", res.secs, res.sparse ? "sparse-space" : "brute force");
        if (res.sparse)
            fprintf(g, "rare mask       0x%x, %ld rare heap sizes (%.3f%%)\n", res.mask, res.nrare,
                    100.0 * res.nrare / N);
        fprintf(g, "max value       %u at n = %ld\n", maxG, maxat);
        if (P.found)
            fprintf(g, "period          %ld, preperiod %ld (%s)\n", P.per, P.pre,
                    P.proven ? "PROVED by the periodicity theorem" : "apparent only, not proved");
        else
            fprintf(g, "period          none found\n");
        fprintf(g, "best lag        %d (%.4f of tail values repeat)\n", peak, amax);
        fprintf(g, "first values    ");
        for (long n = 0; n < N && n < 60; n++) fprintf(g, "%u ", res.G[n]);
        fprintf(g, "\nrecords         ");
        for (int i = 0; i < nrec; i++) fprintf(g, "%u@%ld ", recv[i], recn[i]);
        fprintf(g, "\n");
        if (g == s) {
            fprintf(g, "value frequencies (value: count)\n");
            for (unsigned v = 0; v <= maxG; v++)
                if (cnt[v]) fprintf(g, "  %u: %ld\n", v, cnt[v]);
        }
    }
    fclose(s);
    if (!quiet) printf("output          %s/index.html\n", dir);

    free(ac); free(cnt); free(recn); free(recv); free(res.G);
    return 0;
}
#endif /* OCTAL_NO_MAIN */
