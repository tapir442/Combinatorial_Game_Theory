# Getting started

## Requirements

- A C compiler (gcc or clang) and zlib headers (`zlib1g-dev` on Debian/Ubuntu).
- [uv](https://docs.astral.sh/uv/) for the Python side.  Python ≥ 3.10.

## Build and install

```sh
cd octal-explorer
make                         # ./octal and python/octalgames/liboctal.so
uv sync --extra jupyter      # .venv with numpy, matplotlib, pillow, ipywidgets, pandas, JupyterLab
uv run pytest                # 19 tests (including threads and the docstring examples)
```

`make venv`, `make pytest` and `make lab` are shortcuts for the same steps.

!!! note "The library rebuilds itself"
    `octalgames` recompiles `liboctal.so` on import whenever `octal.c` or `octal.h` is
    newer than the library.  After editing the C code, restart the Jupyter kernel and you
    are running the new code.

## First steps on the command line

```sh
./octal .77 -n 500            # Kayles
```

```text
octal game .77
heap sizes      0 .. 499
time            0.00 s (brute force)
max value       8 at n = 27
period          12, preperiod 71 (PROVED by the periodicity theorem)
...
output          out_.77/index.html
```

Open `out_.77/index.html` in a browser.  Then try a game whose periodicity is unknown:

```sh
./octal .007 -n 200000        # Treblecross, about 6 s
```

## First steps in JupyterLab

```sh
uv run jupyter lab notebooks/explore_octal_games.ipynb
```

```python
import octalgames as og

kayles = og.compute(".77", 2000)
kayles                         # summary table
kayles.plot_wrap(mode="diff")  # blank after the preperiod: period 12

t = og.compute(".007", 100_000)
t.dashboard()
t.explore()
```

## Building this documentation

```sh
uv sync --group docs
uv run mkdocs serve            # http://127.0.0.1:8000
uv run mkdocs build            # static site in site/
```
