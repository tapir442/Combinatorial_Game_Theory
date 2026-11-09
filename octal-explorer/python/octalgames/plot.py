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
              saltus: Optional[int] = None, figsize=(12, 6)):
    """Sequence wrapped in rows of `width` (default: period or best lag).

    A period shows as vertical stripes ('values') or as a blank area ('diff').
    In 'diff' mode cells are dark where G(n) - G(n-W) differs from ``saltus``
    (default: the detected saltus when ``width`` is the detected period, else 0),
    so arithmetic periods become blank too.
    """
    if width is None:
        p = s.period
        width = p.period if p and p.period <= 5000 else s.best_lag()
    if rows is None:
        rows = min(-(-(len(s) - start) // width), 1500)
    if mode == "diff" and saltus is None:
        p = s.period
        saltus = p.saltus if p and p.period == width else 0
    img = s.image(width, mode, start=start, rows=rows, bit=bit, threshold=threshold, saltus=saltus)
    ax = _ax(ax, figsize)
    ax.imshow(img, interpolation="nearest", aspect="auto",
              extent=(0, width, start + rows * width, start))
    ax.set_xlabel("column  (n mod width, shifted by start)")
    ax.set_ylabel("n at row start")
    diff_txt = (f"G(n) − G(n−W) ≠ {saltus} in dark red" if saltus else "G(n) ≠ G(n−W) in dark red")
    what = {"values": "values", "diff": diff_txt, "zero": "P-positions",
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


def plot_folded(s: NimSequence, block: int = 256, kind: str = "both", ax=None, *,
                skip_first: bool = True, min_count: Optional[int] = None, figsize=(12, 7)):
    """Value frequencies folded modulo ``block``.

    Parameters
    ----------
    s
        The sequence.
    block
        Block size, usually a power of two (256 shows the peaks of .7213 and .007).
    kind
        ``'profile'``: average frequency of each residue r = v mod block, relative
        to the block average (1 = average); bars at multiples of block/4 are
        darker.
        ``'heatmap'``: one row per block of values (k*block .. (k+1)*block-1), one
        column per residue, colour = frequency relative to that block's average
        (log scale).  Shows *which* blocks have strong peaks.
        ``'both'``: profile above heat map.
    ax
        Axes for 'profile' or 'heatmap'; for 'both' a pair of axes (or None).
    skip_first
        Leave block 0 out of the profile (see :meth:`NimSequence.folded_profile`).
    min_count
        Blocks with fewer heap sizes are left out of the profile and blank in the
        heat map (default ``8 * block``: at least 8 per value on average).
    """
    import matplotlib.pyplot as plt
    from matplotlib.colors import LogNorm

    if kind not in ("profile", "heatmap", "both"):
        raise ValueError("kind must be 'profile', 'heatmap' or 'both'")
    if kind == "both":
        if ax is None:
            fig, ax = plt.subplots(2, 1, figsize=figsize, layout="constrained",
                                   height_ratios=[1, 1.6])
        plot_folded(s, block, "profile", ax[0], skip_first=skip_first, min_count=min_count)
        plot_folded(s, block, "heatmap", ax[1], skip_first=skip_first, min_count=min_count)
        return ax

    ax = _ax(ax, (figsize[0], figsize[1] * 0.4 if kind == "profile" else figsize[1] * 0.6))
    r = np.arange(block)
    if kind == "profile":
        prof = s.folded_profile(block, skip_first=skip_first, min_count=min_count)
        quarter = max(1, block // 4)
        colors = np.where(r % quarter == 0, "#d72828", "#78829a")
        ax.bar(r, prof, width=1.0, color=colors)
        ax.axhline(1, color="0.3", lw=0.8, ls="--")
        ax.set_xlim(-0.5, block - 0.5)
        ax.set_xlabel(f"v mod {block}")
        ax.set_ylabel("frequency / block average")
        top = int(np.nanargmax(prof)) if np.isfinite(prof).any() else 0
        ax.set_title(f"{s.code}: value frequencies folded mod {block} "
                     f"(peak at r = {top}: {np.nanmax(prof):.1f}× average)")
    else:
        F = s.folded_frequency(block, normalize=True)
        sparse = s.folded_frequency(block).sum(axis=1) < (8 * block if min_count is None else min_count)
        F[sparse] = np.nan                       # too few heap sizes to say anything
        Fm = np.where(np.isfinite(F) & (F > 0), F, np.nan)
        lo = np.nanmin(Fm) if np.isfinite(Fm).any() else 0.1
        im = ax.imshow(Fm, aspect="auto", interpolation="nearest", cmap="magma",
                       norm=LogNorm(vmin=max(lo, 1e-2), vmax=np.nanmax(Fm)),
                       extent=(-0.5, block - 0.5, F.shape[0] * block, 0))
        ax.set_xlabel(f"v mod {block}")
        ax.set_ylabel("block start k·" + str(block))
        ax.set_title(f"{s.code}: frequency of each value relative to its block's average"
                     + (f" ({int(sparse.sum())} sparse block(s) blank)" if sparse.any() else ""))
        plt.colorbar(im, ax=ax, label="× block average (log)")
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


for _f in (plot_values, plot_wrap, plot_autocorr, plot_histogram, plot_first_occurrence, plot_folded, dashboard):
    setattr(NimSequence, _f.__name__, _f)
