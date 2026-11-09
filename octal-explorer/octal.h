/**
 * @file octal.h
 * @brief Public C API of the octal-explorer library (liboctal).
 *
 * Build the library from octal.c with
 *
 *     cc -O3 -shared -fPIC -DOCTAL_NO_MAIN -o liboctal.so octal.c -lz -lm
 *
 * or `make lib`.  The Python package `octalgames` loads it through ctypes.
 *
 * ## Game codes
 *
 * A code is a string such as ".007", "0.6", ".137", "4." or ".f6".  Each
 * character after the point is a hexadecimal digit d_k (k = 1, 2, ...).  Bit j
 * of d_k (value 2^j) allows removing k beans from one heap and leaving exactly
 * j nonempty heaps, for j = 0..3.  An optional digit d_0 before the point allows
 * splitting a heap without removing beans (only bits 2 and 3 matter there).
 * The named rules "grundy" (split into two unequal heaps) and "couples"
 * (Couples-are-forever: split any heap except a heap of 2) are also accepted.
 *
 * ## Conventions
 *
 * - Nim-values are stored as uint16_t.  Values above 65535 are reported as
 *   errors.
 * - **Thread-safe and reentrant.**  The functions keep no global state: each
 *   octal_extend() call allocates its own scratch memory and frees it before
 *   returning.  Calls may run concurrently in different threads, as long as
 *   they do not write to the same array G.
 * - The library never exits the process; errors are reported as return codes.
 * - Return codes: 0 = success, -1 = the code does not parse, -2 = a value
 *   exceeds 65535, -3 = out of memory.
 */
#ifndef OCTAL_H
#define OCTAL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Human-readable name of a rule.
 *
 * @param code  game code, e.g. ".007" or "grundy"
 * @param name  output buffer (NUL-terminated, truncated to @p len)
 * @param len   size of @p name in bytes
 * @return 0, or -1 if @p code does not parse
 */
int octal_rule_name(const char *code, char *name, int len);

/**
 * @brief Compute nim-values G[from..to) from the already known G[0..from).
 *
 * Uses the brute-force mex over all options: O(n) work per heap size for
 * two-heap moves, O(n^2) for three-heap (hexadecimal) moves.  Calling it
 * repeatedly on consecutive ranges gives the same result as one call, so a
 * caller can compute in chunks and stay interruptible.
 *
 * @param code  game code
 * @param G     array of at least @p to entries; G[0..from) must be valid
 * @param from  first heap size to compute (0 to start from scratch)
 * @param to    one past the last heap size to compute
 * @return 0, -1 (bad code), -2 (value > 65535) or -3 (out of memory).
 *         On -2 and -3, G is valid up to the heap size where it stopped.
 */
int octal_extend(const char *code, uint16_t *G, long from, long to);

/**
 * @brief Search for an eventual period of G[0..N).
 *
 * For each candidate period p (smallest first), the preperiod i is the
 * smallest index with G(n) = G(n+p) for all i <= n < N-p.
 *
 * The period is **proved** by the octal periodicity theorem when
 * N >= 2i + 2p + t, where t is the index of the last nonzero code digit.  This
 * is only claimed for plain octal codes: no d_0, no hexadecimal digits >= 8, no
 * named rules.
 *
 * Otherwise the smallest p that repeats at least three times at the end
 * (N - i >= 3p) is reported as **apparent**.
 *
 * @param code  game code (needed for t and for the proof conditions)
 * @param G     nim-values
 * @param N     number of values in @p G
 * @param out   out[0] = preperiod i, out[1] = period p (valid if return > 0)
 * @return 0 = none found, 1 = apparent, 2 = proved, -1 = bad code
 */
int octal_period(const char *code, const uint16_t *G, long N, long *out);

/**
 * @brief Like octal_period(), but also detects arithmetic periodicity.
 *
 * Looks for G(n+p) = G(n) + s for all n >= i, where s >= 0 is the saltus
 * (s = 0 is ordinary periodicity).  For each p, s is taken from the end of the
 * data, s = G(N-1) - G(N-1-p).  Arithmetic periods with s > 0 are only ever
 * reported as apparent (return 1): verifying them needs Austin's theorem or
 * its generalisation by Howse and Nowakowski, which is not implemented.
 *
 * @param code  game code
 * @param G     nim-values
 * @param N     number of values in @p G
 * @param out   out[0] = preperiod i, out[1] = period p, out[2] = saltus s
 * @return 0 = none found, 1 = apparent, 2 = proved (s = 0 only), -1 = bad code
 */
int octal_period_ex(const char *code, const uint16_t *G, long N, long *out);

#ifdef __cplusplus
}
#endif

#endif /* OCTAL_H */
