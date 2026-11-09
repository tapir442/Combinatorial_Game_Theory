"""Benchmark octal-explorer against CGSuite on nim-sequences of take-and-break games.

CGSuite is driven through its headless REPL (org.cgsuite.lang.Repl in
cgsuite-core.jar) over a pipe.  A command is timed from sending it until the next
prompt appears, so JVM startup is excluded.  Before every timed run the REPL
caches are cleared (:clear) and the ruleset is re-created.  Each game gets one
untimed JIT warm-up run.

octal-explorer is timed through the Python package (the C library call only).

    uv run python benchmarks/bench_cgsuite.py                 # default games and sizes
    uv run python benchmarks/bench_cgsuite.py --quick         # small sizes only
    uv run python benchmarks/bench_cgsuite.py --games .007 .6 --sizes 1000 10000

Outputs (default benchmarks/results/): results.csv, report.md, timing.png
"""
from __future__ import annotations

import argparse
import csv
import os
import platform
import select
import statistics
import subprocess
import sys
import time
from pathlib import Path

import numpy as np

import octalgames as og

# our code -> (CGSuite TakeAndBreak code, description)
GAMES = {
    ".77": ("0.77", "Kayles"),
    ".07": ("0.07", "Dawson's Kayles"),
    ".137": ("0.137", "Dawson's chess"),
    ".6": ("0.6", "Officers (open)"),
    ".007": ("0.007", "Treblecross (open)"),
    "grundy": ("4!.0", "Grundy's game (open)"),
    ".f6": ("0.F6", "hexadecimal .f6"),
}
DEFAULT_SIZES = [1000, 2000, 5000, 10000, 20000, 50000, 100000, 200000]
HEX_SIZES = [500, 1000, 2000, 3000, 5000]   # three-heap moves are O(n^2) per heap


class CGSuite:
    """Minimal driver for the CGSuite REPL."""

    def __init__(self, home: Path, heap: str = "8g"):
        java = home / "jre" / "bin" / "java"
        jar = home / "CGSuite" / "modules" / "ext" / "cgsuite-core.jar"
        if not java.exists() or not jar.exists():
            raise SystemExit(f"CGSuite not found under {home} (expected {java} and {jar})")
        self.cmd = [str(java), f"-Xmx{heap}", "-Xss64m", "-cp", str(jar), "org.cgsuite.lang.Repl"]
        t = time.perf_counter()
        self.p = subprocess.Popen(self.cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                  stderr=subprocess.STDOUT, bufsize=0)
        self._read_prompt(120)
        self.startup = time.perf_counter() - t
        self.version = self.eval(":version")[1].strip().splitlines()

    def _read_prompt(self, timeout: float) -> str:
        buf, end = b"", time.time() + timeout
        while not buf.endswith(b"> "):
            left = end - time.time()
            r, _, _ = select.select([self.p.stdout], [], [], max(0.0, left))
            if not r:
                raise TimeoutError(buf[-300:].decode(errors="replace"))
            chunk = os.read(self.p.stdout.fileno(), 1 << 20)
            if not chunk:
                raise EOFError(buf[-300:].decode(errors="replace"))
            buf += chunk
        return buf[:-2].decode(errors="replace")

    def eval(self, line: str, timeout: float = 3600):
        """Run one REPL line; return (seconds, output without the echoed input)."""
        t = time.perf_counter()
        self.p.stdin.write((line + "\n").encode())
        out = self._read_prompt(timeout)
        dt = time.perf_counter() - t
        if out.startswith(line):
            out = out[len(line):]
        if "Error" in out or "Exception" in out:
            raise RuntimeError(f"CGSuite error for {line!r}: {out.strip()[:500]}")
        return dt, out

    def sequence_time(self, code: str, n: int, timeout: float) -> tuple[float, int]:
        """Seconds for NimValueSequence(n) from a cleared state; also returns G(n)."""
        self.eval(":clear")
        self.eval(f'g := game.heap.TakeAndBreak("{code}")')
        dt, out = self.eval(f"s := g.NimValueSequence({n}); s[{n}]", timeout)
        return dt, int(out.strip())

    def sequence(self, code: str, n: int) -> list[int]:
        self.eval(":clear")
        _, out = self.eval(f'game.heap.TakeAndBreak("{code}").NimValueSequence({n})')
        return [int(x) for x in out.strip().strip("[]").split(",")]

    def period(self, code: str, n: int) -> str:
        self.eval(":clear")
        return self.eval(f'game.heap.TakeAndBreak("{code}").CheckPeriodicity({n})')[1].strip()

    def close(self):
        try:
            self.p.stdin.write(b":quit\n")
            self.p.wait(10)
        except Exception:
            self.p.kill()


def ours_time(code: str, n: int, repeat: int) -> tuple[float, int]:
    times, last = [], None
    for _ in range(repeat):
        s = og.compute(code, n + 1, progress=False)
        times.append(s.seconds)
        last = int(s.values[n])
    return min(times), last


def machine_info() -> dict:
    cpu = "unknown"
    try:
        for line in open("/proc/cpuinfo"):
            if line.startswith("model name"):
                cpu = line.split(":", 1)[1].strip()
                break
    except OSError:
        pass
    cc = subprocess.run(["cc", "--version"], capture_output=True, text=True).stdout.splitlines()
    return {"cpu": cpu, "cores": os.cpu_count(), "os": platform.platform(),
            "python": platform.python_version(), "cc": cc[0] if cc else "?"}


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--cgsuite", type=Path, default=Path.home() / "CGSuite")
    ap.add_argument("--games", nargs="+", default=list(GAMES))
    ap.add_argument("--sizes", nargs="+", type=int, default=None)
    ap.add_argument("--repeat", type=int, default=3, help="timed runs per point (minimum is reported)")
    ap.add_argument("--max-seconds", type=float, default=120,
                    help="stop growing n for a game once a CGSuite run takes longer than this")
    ap.add_argument("--check-n", type=int, default=3000, help="sequence length compared value by value")
    ap.add_argument("--quick", action="store_true", help="sizes up to 10,000 and 1 repeat")
    ap.add_argument("--out", type=Path, default=Path(__file__).parent / "results")
    args = ap.parse_args()
    if args.quick:
        args.repeat = 1
        args.sizes = args.sizes or [1000, 2000, 5000, 10000]
    args.out.mkdir(parents=True, exist_ok=True)

    cg = CGSuite(args.cgsuite)
    print(f"CGSuite REPL up in {cg.startup:.2f} s: {' / '.join(cg.version)}", flush=True)
    rows, checks = [], []
    try:
        for code in args.games:
            cgcode, desc = GAMES[code]
            is_hex = code.startswith(".") and any(c in "89abcdef" for c in code[1:])
            if is_hex:
                sizes = HEX_SIZES[:3] if args.quick else (args.sizes or HEX_SIZES)
            else:
                sizes = args.sizes or DEFAULT_SIZES
            # correctness: full sequence and periodicity
            m = min(args.check_n, max(sizes))
            theirs = cg.sequence(cgcode, m)
            s = og.compute(code, m + 1, progress=False)
            ok = theirs == s.values[1:].tolist()
            per_cg = cg.period(cgcode, m) if code in (".77", ".07", ".137") else ""
            checks.append({"game": code, "n": m, "identical": ok, "ours_period": str(s.period or ""),
                           "cgsuite_period": per_cg})
            print(f"\n{code:7s} {desc}: values 1..{m} identical: {ok}"
                  + (f"; ours {s.period}; CGSuite {per_cg}" if per_cg else ""), flush=True)
            if not ok:
                bad = next(i for i, (a, b) in enumerate(zip(theirs, s.values[1:].tolist())) if a != b)
                print(f"  first difference at n = {bad + 1}", flush=True)
            cg.sequence_time(cgcode, min(2000, sizes[0] * 2), 600)   # JIT warm-up
            for n in sizes:
                t_ours, g_ours = ours_time(code, n, args.repeat)
                tcg = []
                for _ in range(args.repeat):
                    dt, g_cg = cg.sequence_time(cgcode, n, timeout=max(600, 20 * args.max_seconds))
                    tcg.append(dt)
                    if dt > args.max_seconds:
                        break
                t_cg = min(tcg)
                if g_cg != g_ours:
                    print(f"  MISMATCH at G({n}): ours {g_ours}, CGSuite {g_cg}")
                rows.append({"game": code, "description": desc, "n": n, "ours_s": round(t_ours, 5),
                             "cgsuite_s": round(t_cg, 5), "speedup": round(t_cg / max(t_ours, 1e-6), 1),
                             "G_n": g_ours, "agree": g_cg == g_ours})
                print(f"  n={n:>7,}  ours {t_ours:9.4f} s   CGSuite {t_cg:9.4f} s   "
                      f"speed-up {t_cg / max(t_ours, 1e-6):7.1f}x", flush=True)
                if t_cg > args.max_seconds:
                    print(f"  (stopping {code}: CGSuite run exceeded {args.max_seconds:.0f} s)")
                    break
    finally:
        cg.close()

    with open(args.out / "results.csv", "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0]))
        w.writeheader()
        w.writerows(rows)
    plot(rows, args.out / "timing.png")
    report(rows, checks, cg, args, args.out / "report.md")
    print(f"\nwrote {args.out}/results.csv, report.md, timing.png")


def plot(rows, path):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    games = list(dict.fromkeys(r["game"] for r in rows))
    fig, axes = plt.subplots(1, 2, figsize=(13, 5), layout="constrained")
    colors = plt.cm.tab10.colors
    for i, g in enumerate(games):
        rs = [r for r in rows if r["game"] == g]
        n = [r["n"] for r in rs]
        c = colors[i % 10]
        axes[0].loglog(n, [r["cgsuite_s"] for r in rs], "o--", color=c, alpha=0.8)
        axes[0].loglog(n, [r["ours_s"] for r in rs], "s-", color=c, label=g)
        axes[1].semilogx(n, [r["speedup"] for r in rs], "o-", color=c, label=g)
    axes[0].set_title("Time to compute G(1..n): solid = octal-explorer, dashed = CGSuite")
    axes[0].set_xlabel("n")
    axes[0].set_ylabel("seconds")
    axes[0].legend(frameon=False, ncol=2)
    axes[0].grid(True, which="both", alpha=0.25)
    axes[1].set_title("Speed-up (CGSuite time / octal-explorer time)")
    axes[1].set_xlabel("n")
    axes[1].set_ylabel("×")
    axes[1].axhline(1, color="0.5", lw=0.8)
    axes[1].grid(True, which="both", alpha=0.25)
    axes[1].legend(frameon=False, ncol=2)
    fig.savefig(path, dpi=120)


def report(rows, checks, cg, args, path):
    mi = machine_info()
    games = list(dict.fromkeys(r["game"] for r in rows))
    L = ["# Benchmark: octal-explorer vs CGSuite", "",
         f"Date: {time.strftime('%Y-%m-%d')}.  Machine: {mi['cpu']} ({mi['cores']} logical cores), {mi['os']}.",
         f"CGSuite: {' / '.join(cg.version)}; REPL startup {cg.startup:.2f} s (excluded).",
         f"octal-explorer: C library via Python {mi['python']}, {mi['cc']}, -O3 -march=native.",
         f"Each point is the minimum of {args.repeat} run(s); CGSuite caches are cleared before every run.",
         "", "![timing](timing.png)", "", "## Correctness", "",
         "| Game | Values compared | Identical | Period (ours) | Period (CGSuite) |", "|---|---|---|---|---|"]
    for c in checks:
        L.append(f"| `{c['game']}` | 1 … {c['n']:,} | {'yes' if c['identical'] else '**NO**'} | "
                 f"{c['ours_period'] or '—'} | {c['cgsuite_period'] or '—'} |")
    L += ["", "## Timings (seconds)", ""]
    for g in games:
        rs = [r for r in rows if r["game"] == g]
        L += [f"### `{g}` — {rs[0]['description']}", "",
              "| n | octal-explorer | CGSuite | speed-up | G(n) agrees |", "|---|---|---|---|---|"]
        for r in rs:
            L.append(f"| {r['n']:,} | {r['ours_s']:.4f} | {r['cgsuite_s']:.4f} | {r['speedup']:.1f}× | "
                     f"{'yes' if r['agree'] else '**no**'} |")
        L.append("")
    sp = [r["speedup"] for r in rows if r["n"] >= 5000]
    if sp:
        L += ["## Summary", "",
              f"For n ≥ 5,000 the speed-up ranges from {min(sp):.1f}× to {max(sp):.1f}× "
              f"(median {statistics.median(sp):.1f}×).", ""]
    L += ["## Method", "",
          "- CGSuite: `TakeAndBreak(code).NimValueSequence(n)` in the headless REPL "
          "(`org.cgsuite.lang.Repl`), timed from sending the line to the next prompt, after `:clear`.",
          "- octal-explorer: `octalgames.compute(code, n+1)`, i.e. `octal_extend` in C (brute-force mex).",
          "- Both are single-threaded algorithms; the JVM may use extra threads for JIT and GC.",
          "- Grundy's game is `4!.0` in CGSuite.  Couples-are-forever has no CGSuite code and is not included.",
          f"- Reproduce: `uv run python benchmarks/bench_cgsuite.py` (options: `--quick`, `--games`, `--sizes`).", ""]
    path.write_text("\n".join(L))


if __name__ == "__main__":
    main()
