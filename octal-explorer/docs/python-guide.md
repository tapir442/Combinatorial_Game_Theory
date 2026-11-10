# Python user guide

```python
import octalgames as og
```

## Computing

```python
s = og.compute(".007", 100_000)     # NimSequence
s.extend(300_000)                    # continue later; existing values are kept
```

- The work runs in the C library in chunks of about 0.3 s, with a progress bar in
  Jupyter.
- **Kernel → Interrupt** stops the computation cleanly and keeps the values computed
  so far.
- Save and resume:

```python
s.save_csv("treblecross.csv")
s = og.NimSequence.from_csv(".007", "treblecross.csv").extend(1_000_000)
```

## Looking at the data

```python
s.values          # read-only numpy uint16 array
s[1000:1010]      # slicing works like an array
len(s), s.max_value
s.period          # Period(preperiod, period, proven, saltus, proof) or None; saltus > 0 = arithmetic
                  # proof: 'periodicity theorem', 'Austin 6.8' or None; s.period.proof_period / .proof_saltus
s.records()       # [(n, value), ...] where a new maximum first appears
s.counts()        # counts[v]
s.first_occurrence()
s.p_positions()   # heap sizes with G = 0
s.autocorr(1000)  # a[p] = match fraction at lag p (second half of the data)
s.folded_frequency(256)   # counts folded mod 256: one row per block of 256 values
s.folded_profile(256)     # average shape inside a block (1 = block average)
s.best_lag()
s.wrap(W)         # 2-D array, -1 padding at the end
s.summary()       # dict; s.to_frame() gives a pandas DataFrame
```

In Jupyter, evaluating `s` shows a summary table.

## Plots (matplotlib)

All plot functions are also methods and accept `ax=` to draw into your own figure.

```python
s.plot_values()                      # G(n) with record values
s.plot_wrap()                        # width = period or best lag
s.plot_wrap(34, mode="diff", rows=100)
s.plot_wrap(53, mode="diff", saltus=16)   # arithmetic period: dark where G(n) - G(n-53) != 16
s.plot_wrap(64, mode="bit", bit=3)
s.plot_wrap(256, mode="ge", threshold=512)
s.plot_autocorr(2000)
s.plot_histogram()
og.plot_first_occurrence(s)
s.plot_folded(256)                   # value frequencies folded modulo 256 (profile + heat map)
s.plot_folded(64, kind="profile")
s.dashboard()                        # all in one figure
```

`mode` is one of `values`, `diff`, `zero` (P-positions), `bit` or `ge` (threshold).  See
[Concepts](concepts.md#reading-the-pictures).

## Interactive explorer

```python
s.explore()          # or og.explore(s, width=12)
```

| Control | Effect |
|---|---|
| width W (box and slider) | values per row |
| −1 / +1 / ×2 / ÷2 | step through widths; stripes appear at periods |
| period / best lag | jump to the detected period or the best lag |
| start n, jump to end | which part of the sequence to show (`jump to end` shows the tail, where periodicity lives) |
| cell px | zoom |
| mode, bit / thr | values, diff, P-positions, bit plane, values ≥ threshold |
| saltus | expected G(n) − G(n−W) in `diff` mode (set by the period button) |

The status line counts the cells with \(G(n) \ne G(n-W)\) in view and shows the last
one.  In `diff` mode at a true period, this is the last exception.

## Surveys

```python
codes = og.octal_codes(2)                    # '.01' ... '.77' with last digit nonzero
df = og.survey(codes, 2000)                  # DataFrame indexed by code
df[df.period.isna()]                         # no period visible
og.survey(og.octal_codes(1, max_digit=15), 3000)   # one-digit hexadecimal games
```

## Colours

```python
og.palette(20)        # (21, 3) uint8 lookup table, same as the C program
og.colormap(20)       # matplotlib ListedColormap
```

## Threads and processes

The C library is thread-safe, and ctypes releases the GIL during each call.  So several
games can be computed **in parallel threads**:

```python
from concurrent.futures import ThreadPoolExecutor

codes = [".6", ".007", ".07", ".137", ".77", "grundy", ".164", ".4"]
with ThreadPoolExecutor(8) as ex:
    seqs = list(ex.map(lambda c: og.compute(c, 60_000, progress=False, method="brute"), codes))
```

On a 12-core machine this is about 4× faster than computing them one after another.
The slowest game (Kayles) limits it.  Pass `method="brute"` here (or call
`og.set_default_method("brute")`): the default `"blocked"` already uses all cores for each game,
so running many games in parallel threads on top of it only oversubscribes the CPU.  Don't call
`extend` on the *same* `NimSequence` from two threads at once.

## Using all cores for one sequence

`method="blocked"`, the default, computes one sequence on several cores.  It gives exactly the
same values as `method="brute"` (one core):

```python
s = og.compute(".007", 1_000_000, method="blocked")             # all cores
s = og.compute(".007", 1_000_000, method="blocked", threads=4)  # 4 threads
s.extend(2_000_000)                    # uses the stored method; override with method=...
```

For `.007` up to \(10^6\) this takes 10 s on 12 cores instead of 143 s.  It does not help for
hexadecimal codes or small `n`.  Don't combine it with many games in parallel threads: each
call already uses all cores.

`method="gpu"` runs the parallel part on a Vulkan GPU instead:

```python
og.gpu_name()                          # 'AMD Radeon ...', or None without a usable GPU
s = og.compute(".007", 1_000_000, method="gpu")
```

`method="rocm"` does the same on an AMD GPU with ROCm (HIP), and `og.rocm_name()` names it.

## Changing the default method

```python
og.set_default_method("gpu")              # all later compute() / NimSequence() / survey() calls
og.set_default_method("blocked", threads=4)
og.get_default_method()                   # 'blocked'
```

Or set it before Python starts, for example for a whole JupyterLab session:

```sh
OCTALGAMES_METHOD=gpu make lab            # OCTALGAMES_THREADS=4 for the blocked thread count
```

A method passed explicitly (`og.compute(code, n, method="brute")`) always wins.  Sequences that
already exist keep their method for later `extend` calls.
Both raise `RuntimeError` if there is no such GPU.  Whether they beat `"blocked"` depends on the
GPU: an integrated GPU with 2 compute units is slower than 12 CPU cores (21–24 s vs 10 s above).
