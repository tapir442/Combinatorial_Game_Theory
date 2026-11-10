# Development

## Layout

```text
octal-explorer/
├── octal.c                  C: rules, mex kernel, period search, PNG/HTML output, CLI
├── octal.h                  public C API of the library
├── Makefile                 octal, lib, test, venv, pytest, lab
├── pyproject.toml           uv project (package octalgames)
├── uv.lock
├── python/octalgames/
│   ├── _lib.py              ctypes binding, auto-build of liboctal.so
│   ├── core.py              NimSequence, Period, compute, survey, palette
│   ├── plot.py              matplotlib plots (attached as NimSequence methods)
│   └── widgets.py           ipywidgets explorer
├── tests/test_octalgames.py
├── notebooks/explore_octal_games.ipynb
├── docs/                    this documentation (MkDocs)
└── mkdocs.yml
```

## Tests

```sh
make test          # C: sparse and blocked vs brute force, Kayles and Dawson's Kayles periods,
                   # and test-threads (8 threads under ThreadSanitizer vs sequential)
make test-gpu      # C: --gpu vs brute force on 5 games (needs a Vulkan GPU)
make test-rocm     # C: --rocm vs brute force on 5 games (needs an AMD GPU with ROCm)
make spv           # recompile octal_gpu.comp into octal_gpu_spv.h after changing the shader
                   # (needs glslangValidator: apt install glslang-tools)
uv run pytest      # Python: C backend vs a pure-Python reference on 7 codes,
                   # periods, extend == one shot, Grundy's game, wrap/image, survey,
                   # threads (same results, real parallel speed-up), doc examples
```

The pure-Python reference in `tests/test_octalgames.py` (`brute`) is the independent
check.  Compare new rule types against it too.

## Code comments

Every type, global and function in `octal.c` and `octal.h` has a Doxygen-style
comment (`/** ... */`, with `@param`, `@return` and `/**< */` for struct fields).
Keep that style for new code.

`make doxygen` builds an HTML reference of the C code into `doxygen/html/index.html`
with the settings in `Doxyfile`.  It includes static functions, browsable source, and
call and caller graphs.  It needs `doxygen` (`apt install doxygen`) and Graphviz.
Warnings go to `doxygen/warnings.log`.  Doxygen is not needed for the normal build.

## Adding a rule type

Example: Pulleyblank's "split only heaps larger than h" (task 1.15).

1. Add an `SP_…` constant and accept a name in `parse_rule`.  If the rule has a
   parameter, parse it from the code (for example `split>3`).
2. Restrict moves in `pair_allowed` (two-heap splits) or `mark_simple`.
3. Make sure `find_period` does not claim a proof: `can_prove` already requires
   `special == SP_NONE`.
4. Add a reference implementation and a test in `tests/`.
5. Document the code syntax in `octal.h` and in `docs/concepts.md`.

## Releasing documentation

```sh
uv run mkdocs build --strict    # fails on broken links or references
```

The site goes to `site/`, which is git-ignored.
