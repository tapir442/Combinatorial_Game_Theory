# octal-explorer

octal-explorer computes the **nim-sequences** (Sprague–Grundy values) of octal and
hexadecimal take-and-break games and draws pictures that make periods and other patterns
visible.

It has two front ends to the same C code:

| | What it gives you |
|---|---|
| **`octal`** (C command-line program) | `summary.txt`, `values.csv`, five PNG plots and a self-contained `index.html` with an interactive explorer |
| **`octalgames`** (Python package) | NumPy arrays, matplotlib plots, an ipywidgets explorer for JupyterLab, and surveys over many games as pandas tables |

```python
import octalgames as og
s = og.compute(".007", 100_000)   # Treblecross
s.period                           # None: no period known (an open problem)
s.explore()                        # interactive wrap explorer
```

## Why

Guy's conjecture says that every finite octal game has an eventually periodic
nim-sequence.  It is still open (Nowakowski, *Unsolved problems in combinatorial games*,
problem A2).  Games such as `.6` (Officers) and `.007` (Treblecross) have been computed to
huge heap sizes without a period appearing.  This tool is for exploring those sequences:
it finds and proves periods where they exist, and shows structure where they don't.

## Contents

- [Getting started](getting-started.md): build, install with uv, first steps.
- [Concepts](concepts.md): game codes, nim-values, the periodicity theorem, and how to
  read the pictures.
- C: the [command line](c-cli.md), the [library API](c-api.md) (`octal.h`), and the
  [internals](c-internals.md).
- Python: the [user guide](python-guide.md) and the [API reference](python-api.md).
- [Development](development.md): tests, project layout, and adding new rule types.
