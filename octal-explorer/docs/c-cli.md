# Command line: `octal`

```text
usage: octal CODE [options]
```

`CODE` is a game code such as `.007`, `0.6`, `.137`, `4.` or `.f6`, or one of the named
rules `grundy` and `couples` (see [Concepts](concepts.md#game-codes)).

## Options

| Option | Default | Meaning |
|---|---|---|
| `-n N` | 10000 | number of heap sizes to compute (0 … N−1) |
| `-o DIR` | `out_CODE` | output directory (created if missing) |
| `-w W` | auto | wrap width for `wrap.png` and `wrapdiff.png`; default is the period if found, else the best autocorrelation lag |
| `-L LAGS` | 1000 | largest autocorrelation lag (at most N/3) |
| `-e M` | 200000 | number of values embedded in `index.html` |
| `-p` | | print the sequence to stdout (30 per line) and write no files |
| `-q` | | quiet: no summary on stdout, no progress |
| `--blocked` | **on** | blocked multi-threaded method on all cores (the default): same values as brute force, much faster for large N (see [Internals](c-internals.md#the-blocked-mode)) |
| `-j THREADS` | all cores | number of threads for `--blocked` |
| `--brute` | off | brute force on one core (the default before the blocked method existed) |
| `--sparse` | off | experimental rare/common-value method (one core) |
| `--gpu` | off | blocked method with the parallel part on a Vulkan GPU (see [Internals](c-internals.md#the-gpu-mode)); exits with an error if there is none |
| `--rocm` | off | same as `--gpu`, on an AMD GPU with ROCm (HIP) instead of Vulkan |
| `--rare` | off | rare-value algorithm (Grossman 2017), one core; much faster for games with few rare values such as `.6`, `grundy`, `couples` (see [Rare-value algorithm](rare-values.md)) |
| `--check` | | compute with the chosen method (`--blocked` unless another is given) and with brute force, compare, and exit (status 1 on mismatch) |

Progress is printed to stderr when stderr is a terminal.

## Output files

| File | Contents |
|---|---|
| `summary.txt` | rule, range, time, period (proved or apparent; with saltus if arithmetic), best lag, first 60 values, record values, frequency of every value |
| `values.csv` | `n,G` for every heap size |
| `values.png` | 1600×640 plot of \(G(n)\) against \(n\), density-shaded, grey lines at powers of two, red record values |
| `wrap.png` | wrapped sequence coloured by value; if there are too many rows, the first and last rows are shown either side of a red line |
| `wrapdiff.png` | same layout, dark red where \(G(n) - G(n-W) \ne s\); \(s\) is the detected saltus when \(W\) is the detected period, else 0 |
| `autocorr.png` | bars for lags 1 … L; red = best lag, orange = multiples of the period |
| `histogram.png` | value frequencies on a log scale, bars in palette colours |
| `index.html` | the above plus records and an interactive canvas explorer (width, cell size, start, saltus, mode: values, diff, P-positions, bit plane, rare values, threshold) |

`index.html` is self-contained: the data is embedded, so it works from `file://` with no
server.

## Examples

```sh
./octal .77 -n 500                    # Kayles: period 12 proved
./octal .007 -n 200000 -o treblecross # open problem
./octal .6 -n 100000 -w 512           # Officers, wrapped at 512
./octal grundy -n 50000
./octal .3303 -n 40 -p                # subtraction game {1,2,4}: 0 1 2 0 1 2 ...
./octal .007 -n 1000000 -j 8           # blocked method on 8 threads
./octal .007 -n 100000 --brute         # one core
./octal .6 -n 20000 --check            # blocked vs brute
./octal .6 -n 20000 --sparse --check   # sparse vs brute
```

## Exit status

0 on success.  1 on an I/O error or a `--check` mismatch.  2 on a usage error.
Unparsable codes and values above 65535 print an error and exit with 1.
