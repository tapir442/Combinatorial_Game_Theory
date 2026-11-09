"""Matplotlib plots for NimSequence (also available as methods: s.plot_values(), ...)."""
from __future__ import annotations

from typing import Optional

import numpy as np

from .core import NimSequence, palette


def _ax(ax, figsize):
    if ax is None:
        import matplotlib.pyplot as plt
        _, ax = plt.subplots(figsize=figsize, layout="constrained")
    return ax


def plot_values(s: NimSequence, ax=None, *, records: bool = True, start: int = 0,
                stop: Optional[int] = None, figsize=(12, 4)):
    """G(n) against n.  Dense ranges are drawn as pixels; record values in red."""
    ax = _ax(ax, figsize)
    stop = len(s) if stop is None else min(stop, len(s))
    n = np.arange(start, stop)
    G = s.values[start:stop]
    if len(n) <= 3000:
        ax.plot(n, G, ".", ms=3, color="#2f5fd0")
    else:
        ax.plot(n, G, ",", color="#2f5fd0", alpha=0.6, rasterized=True)
    if records:
        rec = [(i, v) for i, v in s.records() if start <= i < stop]
        if rec:
            ax.plot(*zip(*rec), "s", ms=4, color="#d72828", label="new record")
            ax.legend(loc="upper left", frameon=False)
    mx = max(1, int(G.max()) if len(G) else 1)
    for k in range(1, int(np.log2(mx)) + 1):
        ax.axhline(2 ** k, color="0.85", lw=0.6, zorder=0)
    ax.set_xlabel("heap size n")
    ax.set_ylabel("G(n)")
    ax.set_title(f"{s.name}: nim-values")
    ax.margins(x=0.01)
    return ax


def plot_wrap(s: NimSequence, width: Optional[int] = None, mode: str = "values", ax=None, *,
              start: int = 0, rows: Optional[int] = None, bit: int = 0, threshold: int = 1,
              figsize=(12, 6)):
    """Sequence wrapped in rows of `width` (default: period or best lag).

    A period shows as vertical stripes ('values') or as a blank area ('diff').
    """
    if width is None:
        p = s.period
        width = p.period if p and p.period <= 5000 else s.best_lag()
    if rows is None:
        rows = min(-(-(len(s) - start) // width), 1500)
    img = s.image(width, mode, start=start, rows=rows, bit=bit, threshold=threshold)
    ax = _ax(ax, figsize)
    ax.imshow(img, interpolation="nearest", aspect="auto",
              extent=(0, width, start + rows * width, start))
    ax.set_xlabel("column  (n mod width, shifted by start)")
    ax.set_ylabel("n at row start")
    what = {"values": "values", "diff": "G(n) ≠ G(n−W) in dark red", "zero": "P-positions",
            "bit": f"bit {bit}", "ge": f"values ≥ {threshold}"}[mode]
    ax.set_title(f"{s.code} wrapped at W = {width}: {what}")
    return ax


def plot_autocorr(s: NimSequence, max_lag: int = 1000, ax=None, *, figsize=(12, 3)):
    """Fraction of n (second half) with G(n) = G(n+p) for p = 1..max_lag."""
    a = s.autocorr(max_lag)
    ax = _ax(ax, figsize)
    lags = np.arange(1, len(a))
    ax.bar(lags, a[1:], width=1.0, color="#78829a")
    p = s.period
    if p and p.period < len(a):
        m = lags[lags % p.period == 0]
        ax.bar(m, a[m], width=1.0, color="#eb8c1e", label=f"multiples of period {p.period}")
        ax.legend(frameon=False)
    best = s.best_lag(max_lag)
    ax.bar([best], [a[best]], width=1.0, color="#d72828")
    ax.annotate(f"best lag {best}", (best, a[best]), textcoords="offset points", xytext=(4, -12))
    ax.set_xlim(0, len(a))
    ax.set_ylim(0, 1.02)
    ax.set_xlabel("lag p")
    ax.set_ylabel("match fraction")
    ax.set_title(f"{s.code}: autocorrelation")
    return ax


def plot_histogram(s: NimSequence, ax=None, *, figsize=(12, 3)):
    """Frequency of each value (log scale); bar colours = palette used in wrap plots."""
    c = s.counts()
    ax = _ax(ax, figsize)
    v = np.arange(len(c))
    lut = palette(max(1, len(c) - 1)) / 255.0
    nz = c > 0
    ax.bar(v[nz], c[nz], width=1.0, color=lut[nz])
    ax.set_yscale("log")
    ax.set_xlabel("value")
    ax.set_ylabel("count")
    ax.set_title(f"{s.code}: value frequencies")
    return ax


def plot_first_occurrence(s: NimSequence, ax=None, *, figsize=(8, 4)):
    """Where each value first appears (log scale) — growth of the value range."""
    f = s.first_occurrence()
    ax = _ax(ax, figsize)
    v = np.flatnonzero(f >= 0)
    ax.plot(v, np.maximum(f[v], 1), ".", ms=3, color="#2f5fd0")
    ax.set_yscale("log")
    ax.set_xlabel("value v")
    ax.set_ylabel("first n with G(n) = v")
    ax.set_title(f"{s.code}: first occurrences")
    return ax


def dashboard(s: NimSequence, width: Optional[int] = None, figsize=(13, 11)):
    """Values, wrap, diff, autocorrelation and histogram in one figure."""
    import matplotlib.pyplot as plt
    fig = plt.figure(figsize=figsize, layout="constrained")
    gs = fig.add_gridspec(4, 2, height_ratios=[1, 1.6, 0.8, 0.8])
    plot_values(s, fig.add_subplot(gs[0, :]))
    plot_wrap(s, width, "values", fig.add_subplot(gs[1, 0]))
    plot_wrap(s, width, "diff", fig.add_subplot(gs[1, 1]))
    plot_autocorr(s, ax=fig.add_subplot(gs[2, :]))
    plot_histogram(s, fig.add_subplot(gs[3, :]))
    return fig


for _f in (plot_values, plot_wrap, plot_autocorr, plot_histogram, plot_first_occurrence, dashboard):
    setattr(NimSequence, _f.__name__, _f)
