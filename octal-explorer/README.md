# octal-explorer

Compute and visualise nim-sequences of octal and hexadecimal take-and-break games:
a C program (`octal`) with PNG and HTML output, plus a Python package
(`octalgames`) that uses the same C code as its backend in JupyterLab.

Full documentation (C command line, C library API, internals, Python guide and API
reference) is in `docs/`.  Read it as a website with `make docs`, which serves it at
http://127.0.0.1:8000.

## Setup (uv)

```sh
make            # builds ./octal and python/octalgames/liboctal.so (needs gcc + zlib)
uv sync --extra jupyter   # creates .venv with numpy, matplotlib, ipywidgets, pandas, jupyterlab
uv run pytest             # 19 tests: C vs a pure-Python reference, known periods, threads, doc examples
uv run jupyter lab notebooks/explore_octal_games.ipynb
```

The Python package rebuilds `liboctal.so` automatically when `octal.c` is newer.  The C code
is `octal.c` (the computation, also the library) and `octal_output.c` (the command-line
program and its output files).

## Game codes

`.d1d2d3…` with hexadecimal digits.  Bit j of digit d_k allows removing k beans and
leaving exactly j nonempty heaps (j = 0, 1, 2, or 3 for hex digits ≥ 8).  An optional
digit before the point allows splitting without removing (`4.` = split any heap).
Named rules: `grundy` (split into two unequal heaps), `couples` (Couples-are-forever).

Examples: `.77` Kayles, `.07` Dawson's Kayles, `.137` Dawson's chess, `.6` Officers,
`.007` Treblecross.

## Python / Jupyter

```python
import octalgames as og
s = og.compute(".007", 100_000)   # interruptible; values so far are kept
s                                  # summary table
s.period                           # Period(preperiod, period, proven, saltus) or None
s.extend(300_000)                  # continue later
s.plot_values(); s.plot_wrap(mode="diff"); s.plot_autocorr(); s.plot_histogram()
s.dashboard()                      # all of the above
s.explore()                        # interactive wrap explorer (ipywidgets)
s.plot_folded(256)                 # value frequencies folded modulo 256
s.wrap(W), s.image(W, mode), s.autocorr(), s.records(), s.counts(), s.to_frame()
og.survey(og.octal_codes(2), 2000) # DataFrame over all 2-digit octal games
```

`proven=True` means the octal periodicity theorem certifies the period: G(n+p) = G(n)
for i ≤ n < 2i + p + t, where t is the last code digit.  Otherwise the period is only
apparent (seen for at least three periods at the end).  Proofs are only claimed for
plain octal codes (no d0, no hex digits, no named rules).

## Command line

```sh
./octal .007 -n 200000          # writes out_.007/{summary.txt,values.csv,*.png,index.html}
./octal .6 -n 100000 -w 512     # choose the wrap width
./octal .77 -n 500 -p           # just print the sequence
./octal .6 -n 10000000 --rare   # rare-value algorithm: 10^7 values of Officers in about 1 minute
make test                       # every method vs brute force, Kayles and Dawson's Kayles periods
```

Methods (all give identical values; choose one, the default is `--blocked`):

| option | method | best for |
|---|---|---|
| `--blocked` (default) | blocked pair scan on all cores (`-j N` threads) | any game |
| `--rare` | rare-value algorithm (Grossman 2017), nearly constant time per value; speculation on all cores (`-j 1`: plain, one core) | games with few rare values: `.6`, `grundy`, `couples` |
| `--gpu` / `--rocm` | blocked method on a Vulkan / ROCm GPU | machines with a strong GPU |
| `--brute` | brute force, one core | reference |
| `--sparse` | older experimental rare/common variant, one core | testing only |

`--check` compares the chosen method with brute force.  See `docs/rare-values.md` and
`benchmarks/results/methods.md` (`make bench-methods`).

`index.html` includes an interactive canvas explorer with the data embedded.  Open it
in a browser.

## Pictures

- **values**: G(n) against n, with record values in red.
- **wrap**: rows of W consecutive values coloured by value.  A period W, or a multiple
  of one, shows as vertical stripes.
- **diff**: dark where G(n) ≠ G(n−W).  Blank after the preperiod when W is a period.
- **autocorrelation**: fraction of the second half with G(n) = G(n+p).  Peaks point to
  candidate periods.
- **histogram**: value frequencies on a log scale.  Short bars are rare values.

## Benchmark

octal-explorer is about 13× faster than CGSuite 2.1.1 on octal games and about 20× faster
on Grundy's game and hexadecimal games, with identical results.  For example, 200,000
values of `.007` take 5.6 s against 73 s.  See `docs/benchmark.md` and
`benchmarks/results/`.  Rerun with `make bench`.

## Notes

- Brute force and the blocked method are O(n) per heap size (O(N²) total).  10⁶ values
  take about 143 s with `--brute` and 10 s with `--blocked` on 24 threads.  `--rare` takes
  1.4 s for 10⁶ and 11 s for 10⁷ values of `.6` (6 s and 62 s with `-j 1`), but gives no
  speed-up for `.007`.
- `--sparse` (CLI only) is an older rare/common-value variant with the opposite convention.
  It is checked against brute force but is not faster; `--rare` replaces it.
- The C library is thread-safe (no global state).  Python threads compute different games
  in parallel, since ctypes releases the GIL.
- Values are stored as 16-bit integers (max 65535).
