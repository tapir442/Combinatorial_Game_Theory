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
  65535.
- **Not thread-safe.**  The functions share a global scratch table.  Serialise calls, or
  use separate processes.
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

On −2, `G` is valid up to the heap size where the overflow happened.

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
