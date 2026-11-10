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
 * - **No internal cache.**  The array G is the only memo: values are computed
 *   bottom-up from the stored values of smaller heaps, each exactly once.  To
 *   avoid recomputation, keep G and pass it back to octal_extend() with
 *   from = number of values already known (the Python NimSequence does this).
 * - The library never exits the process; errors are reported as return codes.
 * - Return codes: 0 = success, -1 = the code does not parse, -2 = a value
 *   exceeds 65535, -3 = out of memory, -4 = no usable GPU (GPU functions only).
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
 * @brief Like octal_extend(), but with the blocked, multi-threaded method.
 *
 * Heap sizes are processed in blocks [n0, n0+B), B ~ n0/32.  The two-heap
 * options whose heaps both lie below n0 (almost all the work) are scanned in
 * parallel with OpenMP; the few options involving heaps of the block itself
 * are added sequentially.  Gives exactly the same values as octal_extend().
 * Heap sizes below 4096 and three-heap (hexadecimal) options are computed
 * sequentially, so small n and hexadecimal codes do not get faster.
 *
 * Without OpenMP (library not built with -fopenmp) it runs in one thread; it
 * is then still somewhat faster than octal_extend() thanks to cache tiling.
 * Reentrant like octal_extend(): every call starts its own thread team.
 *
 * @param code     game code
 * @param G        array of at least @p to entries; G[0..from) must be valid
 * @param from     first heap size to compute
 * @param to       one past the last heap size to compute
 * @param threads  number of threads; <= 0 uses all cores (OMP_NUM_THREADS)
 * @return 0, -1 (bad code), -2 (value > 65535) or -3 (out of memory).
 *         On -2 and -3, G is valid up to the heap size where it stopped.
 */
int octal_extend_blocked(const char *code, uint16_t *G, long from, long to, int threads);

/**
 * @brief Like octal_extend(), but with the rare-value algorithm (Grossman 2017).
 *
 * A value is rare if popcount(v & mask) is even, for a mask chosen from the
 * data so that few heap sizes are rare.  All reachable common values come
 * from the (few) rare heaps; the remaining rare values below the candidate
 * usually have a witness pair among the first few thousand pairs.  So the
 * cost per heap size is nearly constant instead of O(n) when the game has few
 * rare values (.6, .007, ...).  Exact for every game: it falls back to brute
 * force while no good mask exists.  One core; reentrant like octal_extend().
 *
 * J.P. Grossman, "Searching for periodicity in Officers", Games of No Chance 5,
 * MSRI Publications 70 (2017), 373-385.
 *
 * @return 0, -1 (bad code), -2 (value > 65535) or -3 (out of memory)
 */
int octal_extend_rare(const char *code, uint16_t *G, long from, long to);

/**
 * @brief Like octal_extend_blocked(), but with the parallel scan on the GPU.
 *
 * Each block's two-heap options below the block are scanned by a Vulkan
 * compute shader (one workgroup per heap); the rest of the block is finished on
 * one CPU core.  Heap sizes below 32768 are computed with
 * octal_extend_blocked() on all CPU cores.  Gives exactly the same values as
 * octal_extend().
 *
 * Needs a library built with -DOCTAL_VULKAN and linked with -lvulkan, and a
 * Vulkan GPU (CPU implementations such as llvmpipe are only used when the
 * environment variable OCTAL_GPU_ALLOW_CPU is set).  The Vulkan device is
 * created on the first call and kept; calls from several threads are
 * serialised (one at a time on the GPU).
 *
 * @param code  game code
 * @param G     array of at least @p to entries; G[0..from) must be valid
 * @param from  first heap size to compute
 * @param to    one past the last heap size to compute
 * @return 0, -1 (bad code), -2 (value > 65535), -3 (out of memory) or
 *         -4 (no usable GPU, not built with Vulkan, or a Vulkan error).
 *         On -2 and -3, G is valid up to the heap size where it stopped.
 */
int octal_extend_gpu(const char *code, uint16_t *G, long from, long to);

/**
 * @brief Name of the GPU that octal_extend_gpu() uses.
 *
 * @param name  output buffer (NUL-terminated, truncated to @p len)
 * @param len   size of @p name in bytes
 * @return 0, or -4 if there is no usable GPU (or no Vulkan support built in)
 */
int octal_gpu_name(char *name, int len);

/**
 * @brief Like octal_extend_gpu(), but with a HIP kernel on an AMD GPU (ROCm).
 *
 * Keeps the nim-values in device memory and copies explicitly, so it also suits
 * discrete GPUs.  Needs a library built with -DOCTAL_ROCM and linked with
 * octal_rocm.o (compiled by hipcc from octal_rocm.hip) and -lamdhip64.  Same
 * values and return codes as octal_extend_gpu(); -4 if there is no usable ROCm
 * GPU or no ROCm support built in.
 */
int octal_extend_rocm(const char *code, uint16_t *G, long from, long to);

/**
 * @brief Name and architecture of the GPU that octal_extend_rocm() uses.
 * @return 0, or -4 if there is no usable ROCm GPU (or no ROCm support built in)
 */
int octal_rocm_name(char *name, int len);

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
 * @brief Like octal_period(), but also detects and proves arithmetic periodicity.
 *
 * Looks for G(n+p) = G(n) + s for all n >= i, where s >= 0 is the saltus
 * (s = 0 is ordinary periodicity).  For each p, s is taken from the end of the
 * data, s = G(N-1) - G(N-1-p).  An arithmetic period (s > 0) is proved with
 * Austin's Theorem 6.8 (MSc thesis, Calgary 1976) when its hypotheses hold:
 * s a power of 2, p >= t+2, small values before the period, enough 3-heap
 * moves, and the relation checked for e < i < e+7p+t, e = i-1.  The hypotheses
 * may only hold for a multiple m*p, m*s; out[3], out[4] give the certified
 * values.
 *
 * @param code  game code
 * @param G     nim-values
 * @param N     number of values in @p G
 * @param out   out[0] = preperiod i, out[1] = period p, out[2] = saltus s,
 *              out[3] = certified period, out[4] = certified saltus (0 if unproved)
 * @return 0 = none found, 1 = apparent, 2 = proved by the octal periodicity
 *         theorem, 3 = proved by Austin's Theorem 6.8, -1 = bad code
 */
int octal_period_ex(const char *code, const uint16_t *G, long N, long *out);

#ifdef __cplusplus
}
#endif

#endif /* OCTAL_H */
