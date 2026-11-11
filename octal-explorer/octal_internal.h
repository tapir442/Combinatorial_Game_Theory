/**
 * @file octal_internal.h
 * @brief Declarations shared by octal.c (computation) and octal_output.c
 *        (command-line program and output files).  Not part of the library API
 *        (that is octal.h); the functions are hidden in the shared library.
 */
#ifndef OCTAL_INTERNAL_H
#define OCTAL_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

/** Marks internal functions shared between the two files: not exported from liboctal. */
#define OCTAL_INTERNAL __attribute__((visibility("hidden")))

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

/** @brief How compute() computes the nim-values (command-line options of the same name). */
typedef enum {
    METHOD_BRUTE,    /**< `--brute`: grundy_brute(), one core */
    METHOD_SPARSE,   /**< `--sparse`: grundy_sparse() where it applies, one core */
    METHOD_BLOCKED,  /**< `--blocked` (default): extend_blocked(), all cores */
    METHOD_VULKAN,   /**< `--gpu`: extend_device() with the Vulkan backend */
    METHOD_ROCM,     /**< `--rocm`: extend_device() with the ROCm backend */
    METHOD_RARE      /**< `--rare`: extend_rare(), Grossman's rare-value algorithm (speculation with -j != 1) */
} Method;

/** Short name of each Method, as used in messages and summary.txt. */
/** Rare-value mask flags (see rv_rare() in octal.c): a position mask, whose rare
 *  class alternates with the heap size n: parity(G(n) & mask) == (n + par) mod 2. */
#define RV_POS (1u << 31)   /**< position mask */
#define RV_PAR (1u << 30)   /**< position mask with par = 1 */
#define RV_MASK 0xffffu     /**< the bits of the mask proper */

/** Names of the methods, indexed by Method (as in the command-line options). */
OCTAL_INTERNAL extern const char *const method_name[];

/** @brief Statistics of the rare-value method (for summary.txt and the benchmark). */
typedef struct {
    int active;               /**< the rare-value method was in use at the end */
    unsigned mask;            /**< mask (valid if active); RV_POS / RV_PAR for a position mask */
    long nrare;               /**< rare heap sizes (valid if active) */
    long long scan1, scan2;   /**< pairs examined in step 1 (rare heaps) and step 2 (witnesses) */
    long long full;           /**< heap sizes where step 2 ran to the end (a new rare value) */
    int threads;              /**< threads: 1 = plain algorithm, > 1 = speculation + parallel check */
    long long speculated;     /**< values computed speculatively (step 1 only) */
    long long restarts;       /**< speculative chunks that failed the check (a new rare value) */
    long long bytes;          /**< speculative values computed with one byte per value (RareBytes) */
    int baton;                /**< size of the speculation team (Baton; 1 = one thread) */
} RareStats;

/** @brief Result of compute(): the nim-sequence plus statistics. */
typedef struct {
    uint16_t *G;         /**< G[0..N): nim-values (owned; free() it) */
    long N;              /**< number of heap sizes computed */
    double secs;         /**< wall-clock seconds */
    int sparse;          /**< sparse method was active at the end */
    unsigned mask;       /**< rare/common mask (valid if sparse) */
    long nrare;          /**< number of rare heap sizes (valid if sparse) */
    long long pairs;     /**< two-heap pairs examined (work counter) */
    Method method;       /**< method that was requested */
    RareStats rare;      /**< statistics of METHOD_RARE */
    int threads;         /**< threads used by the blocked method */
} Result;

/** @brief Eventual (arithmetic) period found by find_period(). */
typedef struct {
    long pre, per;       /**< preperiod i and period p: G(n+p) = G(n) + saltus for n >= i */
    int found, proven;   /**< found: a period was detected; proven: certified by the theorem */
    long saltus;         /**< saltus s (0 = ordinary periodicity) */
    int proof;           /**< 0 none, 1 octal periodicity theorem, 2 Austin's Theorem 6.8 */
    long proof_per;      /**< period that the proof certifies (a multiple of per for Austin) */
    long proof_saltus;   /**< saltus that the proof certifies */
} Period;

/** Print "octal: msg" to stderr and exit with status 1. */
OCTAL_INTERNAL void die(const char *msg);
/** malloc() that never returns NULL (dies instead); n = 0 allocates 1 byte. */
OCTAL_INTERNAL void *xmalloc(size_t n);
/** calloc() that never returns NULL (dies instead). */
OCTAL_INTERNAL void *xcalloc(size_t n, size_t s);
/** Monotonic clock in seconds, for timing. */
OCTAL_INTERNAL double now(void);
/** Parse a game code (see octal.c) into @p R; 0, or -1 if it does not parse. */
OCTAL_INTERNAL int parse_rule(const char *s, Rule *R);
/** Compute G[0..N) of rule @p R with @p method (see octal.c); dies on errors. */
OCTAL_INTERNAL Result compute(const Rule *R, long N, Method method, int threads, int progress);
/** Find an eventual period of G[0..N), optionally with a saltus (see octal.c). */
OCTAL_INTERNAL Period find_period(const uint16_t *G, long N, const Rule *R, int allow_saltus);

#endif /* OCTAL_INTERNAL_H */
