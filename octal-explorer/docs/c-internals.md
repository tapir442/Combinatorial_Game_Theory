# C internals

`octal.c` is one file of about 950 lines, in these sections.

## Rules

```c
typedef struct {
    unsigned d[MAXDIG + 1]; /* d[0] .. d[t]; bit j = may leave j heaps */
    int t;                  /* index of the last nonzero digit */
    int special;            /* SP_NONE, SP_GRUNDY, SP_COUPLES */
    int hex;                /* some digit has bit 3 (three heaps) */
    char code[80], name[120];
} Rule;
```

`parse_rule` turns a code string into a `Rule`.  `MAXDIG` is 64, so codes can have up
to 64 digits.  Bits 0 and 1 of `d[0]` are cleared because they would describe
non-moves.

## The mex kernel

The nim-value of `n` is the smallest value not marked by any option.  Instead of
clearing a boolean array for every `n`, the code uses a **stamp table**:
`seen[v] == n` means "v is an option of n".  It is reset (`memset` to 0xFF) only at the
start of each computation, which is why the library is not thread-safe.

- `mark_simple` marks 0-heap, 1-heap and 3-heap options.  Three-heap moves enumerate
  `a ≤ b ≤ c`.
- `grundy_brute` additionally marks every two-heap split `a + b = n − k` with
  `1 ≤ a ≤ b`, skipping `a == b` for Grundy's game and `n == 2` for Couples.  It then
  returns `mex(st)`.
- `mex(st)` and `mex_from(start, st)` return the smallest value (≥ `start`) not marked
  with stamp `st`.  The sparse mode uses `mex_from` to resume from its current lower
  bound.
- The size of `seen` grows as values grow (`ensure_seen`).  The table stays twice the
  smallest power of two above the largest value, so every XOR of values fits.

## The sparse mode

`grundy_sparse` implements a rare/common split.  Value \(v\) is *rare* if
`popcount(v & mask)` is odd.  An XOR is rare exactly when one operand is rare.  So:

1. All rare reachable values come from pairs with a rare heap.  Iterating over the
   (few) rare heap sizes marks all of them.
2. The current lowest unmarked value `lo` is tracked during the full pair scan.  As soon
   as `lo` is rare it is certainly unreachable, and the scan stops.

`choose_mask` picks the mask that makes the fewest heap sizes rare (re-checked at
n = 2048, 8192, …).  The mode falls back to brute force if more than 25% are rare.

**Limitation.** For `.6` and `.007` the nim-value itself is usually *common*.  Proving
that a common value is unreachable still needs the full scan, so this mode is not
faster there.  A real speed-up needs Grossman's approach (GONC 5); this is task 1.12 in
`proposals/tasks/`.  `--check` verifies the mode against brute force.

## Period search

`find_period` scans backwards from the end for each \(p\) to find the smallest
preperiod.  It applies the octal periodicity theorem when the rule is plain octal (see
[Concepts](concepts.md#periods-proved-and-apparent)).

## Graphics

- `Image` is a plain RGB buffer.  `png_save` writes it with zlib (`compress2`) and
  `crc32`, filter type 0 on every row.
- `palette(v)` is the shared colour scheme (golden-ratio hues, 0 near-black).
- `plot_values` accumulates a density grid and shades it on a log scale.
  `plot_wrap` and `wrap_layout` lay out rows and, for long sequences, keep the first and
  last halves.  The remaining plots are `plot_autocorr` and `plot_hist`.
- `write_html` writes the gallery and the explorer.  The data is a `Uint16Array`
  literal, and the JavaScript uses the same palette formula.

## Library entry points

`octal_rule_name`, `octal_extend` and `octal_period` (see [Library API](c-api.md)) wrap
`parse_rule`, `grundy_brute` and `find_period`.  Everything after
`#ifndef OCTAL_NO_MAIN` (the usage text and `main`) is the command-line program.
