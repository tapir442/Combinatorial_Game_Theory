#!/usr/bin/env python3
"""Benchmark the computation methods of ./octal: brute, blocked, rare (and optionally gpu, rocm).

Each run is `./octal CODE -n N -p --METHOD` (wall-clock time, values printed to stdout).
All methods that ran for the same (code, n) must print identical values, and the
rare-value runs at the largest n are checked against the blocked run on their common prefix.

    python benchmarks/bench_methods.py            # full plan (about 15 minutes)
    python benchmarks/bench_methods.py --quick    # small sizes only (about 1 minute)
    python benchmarks/bench_methods.py --gpu      # also --gpu and --rocm where available

Writes results/methods.csv and results/methods.md next to this file (or --out DIR).
Run it on an otherwise idle machine: the blocked method uses all cores.
"""
from __future__ import annotations

import argparse
import csv
import os
import subprocess
import sys
import time
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
OCTAL = HERE.parent / "octal"

# games: Officers, Grundy's game and Couples-are-forever have few rare values;
# Treblecross (.007) does not (about 10% of heap sizes are rare for the best mask)
GAMES = [".6", "grundy", "couples", ".007"]
# (n, methods); brute force and blocked are O(n^2), so they stop earlier
FULL = [(100_000, ["brute", "blocked", "rare"]),
        (300_000, ["brute", "blocked", "rare"]),
        (1_000_000, ["blocked", "rare"]),
        (10_000_000, ["rare"])]
QUICK = [(50_000, ["brute", "blocked", "rare"]),
         (200_000, ["blocked", "rare"])]
# the rare-value method is only fast for these games; elsewhere it falls back to brute
# force, so it is skipped beyond 3*10^5 there
SPARSE = {".6", "grundy", "couples"}


def run(code: str, n: int, method: str):
    """(seconds, values) of one ./octal run."""
    t = time.perf_counter()
    r = subprocess.run([str(OCTAL), code, "-n", str(n), "-p", "-q", f"--{method}"],
                       capture_output=True, text=True)
    dt = time.perf_counter() - t
    if r.returncode != 0:
        raise RuntimeError(f"{code} -n {n} --{method}: {r.stderr.strip()}")
    return dt, np.array(r.stdout.split(), dtype=np.uint16)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--quick", action="store_true", help="small sizes only")
    ap.add_argument("--gpu", action="store_true", help="also run --gpu and --rocm (if they work)")
    ap.add_argument("--out", type=Path, default=HERE / "results", help="output directory")
    args = ap.parse_args()
    if not OCTAL.exists():
        print(f"{OCTAL} not found: run make first", file=sys.stderr)
        return 1
    plan = QUICK if args.quick else FULL
    extra = []
    if args.gpu:
        for m in ("gpu", "rocm"):
            try:
                run(".6", 40_000, m)
                extra.append(m)
            except RuntimeError as e:
                print(f"skipping --{m}: {e}", file=sys.stderr)

    rows, reference = [], {}
    for code in GAMES:
        for n, methods in plan:
            methods = list(methods) + ([m for m in extra if n <= 1_000_000] if "blocked" in methods else [])
            if code not in SPARSE and n > 1_000_000:
                continue
            if code not in SPARSE and n > 300_000:   # rare falls back to brute force there
                methods = [m for m in methods if m != "rare"]
            values = {}
            for m in methods:
                dt, v = run(code, n, m)
                values[m] = v
                rows.append({"code": code, "n": n, "method": m, "seconds": round(dt, 3),
                             "values_per_second": round(n / dt)})
                print(f"{code:8} n={n:>11,}  {m:8} {dt:9.2f} s", flush=True)
            first = next(iter(values.values()))
            for m, v in values.items():
                if not np.array_equal(v, first):
                    raise SystemExit(f"MISMATCH: {code} n={n} {m} differs from {methods[0]}")
            if "blocked" in values:
                reference[code] = values["blocked"]
            elif code in reference:  # rare-only size: compare the common prefix
                ref = reference[code]
                if not np.array_equal(first[:len(ref)], ref):
                    raise SystemExit(f"MISMATCH: {code} n={n} differs from the blocked reference")
                print(f"{'':8} first {len(ref):,} values equal the blocked run", flush=True)

    args.out.mkdir(parents=True, exist_ok=True)
    with open(args.out / "methods.csv", "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0]))
        w.writeheader()
        w.writerows(rows)
    write_markdown(rows, args.out / "methods.md")
    print(f"wrote {args.out / 'methods.csv'} and {args.out / 'methods.md'}")
    return 0


def write_markdown(rows, path: Path) -> None:
    methods = [m for m in ("brute", "blocked", "gpu", "rocm", "rare") if any(r["method"] == m for r in rows)]
    table = {(r["code"], r["n"], r["method"]): r["seconds"] for r in rows}
    keys = sorted({(r["code"], r["n"]) for r in rows}, key=lambda k: (GAMES.index(k[0]), k[1]))
    cpu = os.cpu_count()
    lines = [f"# Methods benchmark", "",
             f"Wall-clock seconds of `./octal CODE -n N -p --METHOD` ({cpu} logical CPUs).  "
             "All methods gave identical values.  `rare` and `brute` use one core, `blocked` all cores.", "",
             "| code | n | " + " | ".join(methods) + " | rare vs best other |",
             "|---|---:|" + "---:|" * len(methods) + "---:|"]
    for code, n in keys:
        cells = [f"{table[(code, n, m)]:.2f}" if (code, n, m) in table else "–" for m in methods]
        others = [table[(code, n, m)] for m in methods if m != "rare" and (code, n, m) in table]
        ratio = (f"{min(others) / table[(code, n, 'rare')]:.1f}×"
                 if others and (code, n, "rare") in table else "–")
        lines.append(f"| `{code}` | {n:,} | " + " | ".join(cells) + f" | {ratio} |")
    path.write_text("\n".join(lines) + "\n")


if __name__ == "__main__":
    sys.exit(main())
