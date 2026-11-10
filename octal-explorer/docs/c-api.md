# C library API (`octal.h`)

The library is `octal.c` compiled without its `main` function:

```sh
make lib
# or by hand:
cc -O3 -shared -fPIC -DOCTAL_NO_MAIN -o liboctal.so octal.c -lz -lm
```

Include `octal.h` and link with `-loctal` (or load the `.so` dynamically, as the Python
package does).

## Conventions

- Nim-values are `uint16_t`.  Values above 65535 are an error.
- Return codes: `0` success, `-1` the game code does not parse, `-2` a value exceeds
  65535, `-3` out of memory.  The library never exits the process.
- **Thread-safe and reentrant.**  There is no global state: every `octal_extend` call
  allocates its own scratch table and frees it before returning.  Calls can run at the
  same time in different threads, as long as they write to different arrays `G`.
  `make test-threads` checks this under ThreadSanitizer.
- No function keeps a pointer to caller memory after it returns.

## Functions

### `octal_rule_name`

```c
int octal_rule_name(const char *code, char *name, int len);
```

Writes a human-readable name (for example `"octal game .007"` or
`"Grundy's game (split a heap into two unequal heaps)"`) into `name`, truncated to
`len` bytes including the NUL.  Returns 0, or −1 if `code` does not parse.  Use it to
validate codes.

### `octal_extend`

```c
int octal_extend(const char *code, uint16_t *G, long from, long to);
```

Computes `G[from]` … `G[to-1]`.  `G[0..from)` must already hold the correct values, and
`G` must have room for `to` entries.

Computing in chunks gives the same result as one call, so a caller can stay responsive
or interruptible, and can resume from saved values:

```c
uint16_t *G = calloc(N, sizeof *G);
for (long a = 0; a < N; a += 10000) {
    long b = a + 10000 < N ? a + 10000 : N;
    if (octal_extend(".007", G, a, b) != 0) { /* handle error */ }
    report_progress(b);
}
```

On −2 or −3, `G` is valid up to the heap size where the computation stopped.

### `octal_extend_blocked`

```c
int octal_extend_blocked(const char *code, uint16_t *G, long from, long to, int threads);
```

Same contract and return codes as `octal_extend`, and exactly the same values, but computed with
the blocked method on `threads` threads (≤ 0: all cores, or `OMP_NUM_THREADS`).  Much faster for
large `n` (about 14× on 12 cores at \(n = 10^6\)); no faster for heap sizes below 4096 or for
hexadecimal codes.  It needs a library built with `-fopenmp` (the Makefile and the Python package
do this); otherwise it runs in one thread.  Each call starts its own thread team, so it is
reentrant like `octal_extend`.  See [Internals](c-internals.md#the-blocked-mode).

### `octal_extend_gpu`

```c
int octal_extend_gpu(const char *code, uint16_t *G, long from, long to);
int octal_gpu_name(char *name, int len);
```

Same contract as `octal_extend`, and exactly the same values, with the parallel part of the
blocked method on a GPU (Vulkan compute shader).  Returns −4 if there is no usable GPU, or if
the library was built without `-DOCTAL_VULKAN -lvulkan` (the Makefile does this automatically
when the Vulkan headers are installed).  `octal_gpu_name` writes the device name and returns 0,
or returns −4.  The device is created on the first call and kept; concurrent calls run one after
another.  See [Internals](c-internals.md#the-gpu-mode).

### `octal_period`

```c
int octal_period(const char *code, const uint16_t *G, long N, long *out);
```

Searches `G[0..N)` for an eventual period and writes `out[0] = preperiod` and
`out[1] = period`.

| Return | Meaning |
|---|---|
| 2 | **proved** by the octal periodicity theorem (\(N \ge 2i + 2p + t\)) |
| 1 | **apparent**: repeats at least three times at the end, not proved |
| 0 | none found (`out` undefined) |
| −1 | bad code |

The search tries \(p = 1, 2, \dots, N/2\) and returns the first proved period.  If none is
proved, it returns the smallest apparent one.  Multiples of an apparent period are
skipped, so a periodic sequence costs \(O(N)\) per candidate rather than \(O(N^2/p)\).

### `octal_period_ex`

```c
int octal_period_ex(const char *code, const uint16_t *G, long N, long *out);
```

Like `octal_period`, but it also detects **arithmetic periodicity** \(G(n+p) = G(n) + s\)
with saltus \(s \ge 0\), and proves it with Austin's Theorem 6.8 where possible.  `out`
needs **five** entries:

| Entry | Meaning |
|---|---|
| `out[0]` | preperiod |
| `out[1]` | least period |
| `out[2]` | saltus (0 = ordinary periodicity) |
| `out[3]` | period certified by the proof (a multiple of `out[1]` for Austin), 0 if unproved |
| `out[4]` | saltus certified by the proof, 0 if unproved |

| Return | Meaning |
|---|---|
| 3 | proved by Austin's Theorem 6.8 (arithmetic, hexadecimal codes) |
| 2 | proved by the octal periodicity theorem |
| 1 | apparent |
| 0 | none found |
| −1 | bad code |

See [Concepts](concepts.md#austins-theorem-68).  The Python package and the command-line
program use this function.

## Minimal example

```c
#include <stdio.h>
#include <stdlib.h>
#include "octal.h"

int main(void) {
    long N = 1000, pp[2];
    uint16_t *G = calloc(N, sizeof *G);
    if (octal_extend(".77", G, 0, N)) return 1;
    int r = octal_period(".77", G, N, pp);
    printf("Kayles: %s period %ld, preperiod %ld\n",
           r == 2 ? "proved" : r == 1 ? "apparent" : "no", pp[1], pp[0]);
    free(G);
    return 0;
}
```

```sh
cc example.c -L. -loctal -Wl,-rpath,. -o example && ./example
# Kayles: proved period 12, preperiod 71
```
