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
`seen[v] == n` means "v is an option of n".  New entries are 0xFFFFFFFF, which never
equals a valid `n`.

The table lives in a `Scratch` struct (`seen`, `cap`) that belongs to a single
computation.  `compute` and `octal_extend` create it, grow it with `scratch_reserve`
(which returns −1 on out-of-memory instead of exiting), and release it with
`scratch_free`.  The kernel functions take a `Scratch *`.  There are no mutable globals
apart from the command-line flag `quiet`, so the library is thread-safe.

- `mark_simple` marks 0-heap, 1-heap and 3-heap options.  Three-heap moves enumerate
  `a ≤ b ≤ c`.
- `grundy_brute` additionally marks every two-heap split `a + b = n − k` with
  `1 ≤ a ≤ b`, skipping `a == b` for Grundy's game and `n == 2` for Couples.  It then
  returns `mex(S, st)`.
- `mex(S, st)` and `mex_from(S, start, st)` return the smallest value (≥ `start`) not
  marked with stamp `st` in `S`.  The sparse mode uses `mex_from` to resume from its current lower
  bound.
- The size of `seen` grows as values grow (`ensure_seen`).  The table stays twice the
  smallest power of two above the largest value, so every XOR of values fits.

## The blocked mode

`extend_blocked` (CLI `--blocked`, library `octal_extend_blocked`) computes a block of
heap sizes \([n_0, n_0+B)\) together, with \(B \approx n_0/32\) (at most 65536, and at most
64 MB of marks).  For a pair \(a + b = m\) with \(a < b\):

- if \(b < n_0\), both values are known before the block starts.  `block_scan` marks all
  these pairs for all heaps of the block **in parallel** (OpenMP).  Each thread owns a range of
  heaps and its own rows of a byte array `marks[B][vcap]`, so there are no atomics.  It walks
  \(a\) in tiles of 4096, so that `G[a]` and `G[m-a]` stay in cache across its heaps.
- if \(b \ge n_0\), then \(a = m - b < B\).  `block_finish` adds these few pairs, the pair
  \(a = b\) and all 0-, 1- and 3-heap options (`mark_simple`) **sequentially**, in order of
  \(n\).  It then takes the mex over the `marks` row and the stamp table.

The sequential part is about \(B/n_0 \approx 1/32\) of the pair work.  Heap sizes below 4096 use
`grundy_brute`.  Three-heap (hexadecimal) options stay sequential, so hexadecimal codes do not
get faster.  Without `-fopenmp` the pragma is ignored and the mode runs in one thread.

Measured on `.007`, \(N = 10^6\), 12 cores / 24 threads: brute force 143 s; blocked with 1
thread 80 s (cache tiling alone), 4 threads 23 s, 12 threads 10.3 s, 24 threads 9.5 s.
`--blocked --check` and `make test` compare it with brute force.

## The GPU mode

`extend_gpu` (CLI `--gpu`, library `octal_extend_gpu`) is the blocked mode with `block_scan`
replaced by the Vulkan compute shader `octal_gpu.comp`:

- one workgroup of 256 invocations per heap of the block.  They stride over \(a\) and OR the
  values \(G(a) \oplus G(m-a)\) into a bitset in shared memory (they read the bit first, so the
  few, heavily hit words see few atomics).  The bitset is then written to a marks buffer;
- `block_finish` (with `bitset = 1`) completes the block on one CPU core, as before.  Blocks are
  smaller (\(B \approx n_0/128\)), because the GPU waits during this sequential part;
- heap sizes below 32768 are computed by `extend_blocked` on all CPU cores;
- the nim-values live in one host-visible buffer (two `uint16` per `uint32`).  The CPU appends
  each finished block to it.  This needs no copies on integrated GPUs and on discrete GPUs with
  resizable BAR; on other discrete GPUs the shader reads over PCIe, and a staging buffer would
  be the next step.

The Vulkan device and pipeline are created on first use (`gpu_init`) and kept; a mutex
serialises GPU calls.  CPU devices (llvmpipe) are skipped unless `OCTAL_GPU_ALLOW_CPU` is set.
The shader is compiled to SPIR-V by `make spv` and embedded as `octal_gpu_spv.h` (committed,
so building needs only the Vulkan headers and `libvulkan`).  The code is compiled only with
`-DOCTAL_VULKAN`; otherwise the GPU functions return −4.

Measured on `.007`, \(N = 10^6\), with the only GPU of the test machine, the integrated Radeon of
a Ryzen 9 7900X (2 compute units): 23.6 s.  That is 3.3× one CPU core (79 s) but slower than
`--blocked` on all 24 threads (9.8 s).  A discrete GPU has 20–100× more compute units.
`--gpu --check`, `make test-gpu` and the GPU tests in pytest compare it with brute force.

### Backends

Both GPU modes share one driver, `extend_device`, which takes a `Device`: a small set of
functions to start a computation, scan one block, upload finished values and clean up.

| | `--gpu` (Vulkan) | `--rocm` (HIP) |
|---|---|---|
| kernel | `octal_gpu.comp` (GLSL → SPIR-V, embedded) | `octal_rocm.hip` (`hipcc`, linked as `octal_rocm.o`) |
| GPUs | any Vulkan GPU | AMD GPUs supported by ROCm |
| memory | host-visible buffers, no copies | device memory, explicit `hipMemcpy` (better on discrete GPUs) |
| build switch | `-DOCTAL_VULKAN -lvulkan` | `-DOCTAL_ROCM octal_rocm.o -lamdhip64 -lstdc++` |

The ROCm kernel is compiled with `-mwavefrontsize64`.  On RDNA GPUs HIP defaults to wave32,
which is 1.8× slower for this kernel.  Copies to and from the device take under 0.1% of the time.

Measured on `.007`, \(N = 10^6\), Ryzen 9 7900X iGPU (`gfx1036`, 2 compute units; not officially
supported by ROCm, but it works with Ubuntu's ROCm 7.1):

| method | time |
|---|---|
| `--blocked`, 24 CPU threads | 9.8 s |
| `--rocm` | 20.8 s |
| `--gpu` (Vulkan) | 23.3 s |
| `--blocked -j 1` | 79 s |

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
`proposals/tasks/`.  `--sparse --check` verifies the mode against brute force.

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
