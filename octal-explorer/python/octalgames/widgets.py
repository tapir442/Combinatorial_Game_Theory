"""Interactive ipywidgets explorer: s.explore() in JupyterLab."""
from __future__ import annotations

import io

import numpy as np

from .core import NimSequence


def _png(img: np.ndarray) -> bytes:
    from PIL import Image
    buf = io.BytesIO()
    Image.fromarray(img).save(buf, format="PNG")
    return buf.getvalue()


def explore(s: NimSequence, width=None, *, max_pixels: int = 1400):
    """Wrap explorer: change the width W, start, mode and cell size live.

    When W hits a period (or a multiple of one) the picture becomes vertical
    stripes; in 'diff' mode it becomes blank below the preperiod.
    """
    import ipywidgets as w

    p = s.period
    if width is None:
        width = p.period if p and p.period <= 4000 else s.best_lag()

    W = w.BoundedIntText(value=width, min=1, max=max(1, min(20000, len(s))), description="width W",
                         layout=w.Layout(width="180px"))
    Wsl = w.IntSlider(value=min(width, 2000), min=1, max=max(2, min(2000, len(s) // 2)), readout=False,
                      layout=w.Layout(width="420px"))
    w.jslink((W, "value"), (Wsl, "value"))
    start = w.BoundedIntText(value=0, min=0, max=max(0, len(s) - 1), step=1, description="start n",
                             layout=w.Layout(width="200px"))
    cell = w.BoundedIntText(value=4, min=1, max=24, description="cell px", layout=w.Layout(width="150px"))
    mode = w.Dropdown(options=[("values", "values"), ("diff  G(n) − G(n−W) ≠ saltus", "diff"),
                               ("P-positions", "zero"), ("bit plane", "bit"), ("values ≥ threshold", "ge")],
                      value="values", description="mode", layout=w.Layout(width="260px"))
    bit = w.BoundedIntText(value=0, min=0, max=15, description="bit / thr", layout=w.Layout(width="160px"))
    saltus = w.IntText(value=p.saltus if p and p.period == width else 0, description="saltus",
                       layout=w.Layout(width="150px"),
                       tooltip="diff mode compares G(n) - G(n-W) with this value")
    btns = [w.Button(description=d, layout=w.Layout(width="52px")) for d in ("−1", "+1", "×2", "÷2")]
    per_btn = w.Button(description=f"period {p.period}" if p else "best lag",
                       layout=w.Layout(width="120px"))
    tail_btn = w.Button(description="jump to end", layout=w.Layout(width="110px"))
    info = w.HTML()
    img = w.Image(format="png")

    def rows_for(Wv, cs):
        return max(1, min(-(-(len(s) - start.value) // Wv), max_pixels // cs))

    def redraw(*_):
        Wv = max(1, W.value)
        cs = max(1, min(cell.value, max(1, 4000 // Wv)))
        rows = rows_for(Wv, cs)
        a = s.image(Wv, mode.value, start=start.value, rows=rows, bit=bit.value,
                    threshold=bit.value, cell=cs, saltus=saltus.value)
        img.value = _png(a)
        img.layout.width = f"{a.shape[1]}px"
        end = min(len(s), start.value + rows * Wv)
        extra = ""
        if Wv < len(s):
            G = s.values
            lo = max(start.value, Wv)
            if end > lo:
                d = G[lo:end].astype(np.int64) - G[lo - Wv:end - Wv].astype(np.int64)
                mism = np.flatnonzero(d != saltus.value)
                last = f", last at n = {lo + mism[-1]}" if len(mism) else ""
                rel = f"G(n) − G(n−{Wv}) ≠ {saltus.value}" if saltus.value else f"G(n) ≠ G(n−{Wv})"
                extra = f" · {rel} at {len(mism):,} places in view{last}"
        info.value = (f"<code>rows {rows}, n = {start.value:,} … {end - 1:,} · values ≤ "
                      f"{int(s.values[start.value:end].max()) if end > start.value else 0}{extra}</code>")

    def setW(v):
        W.value = max(1, int(v))

    btns[0].on_click(lambda _: setW(W.value - 1))
    btns[1].on_click(lambda _: setW(W.value + 1))
    btns[2].on_click(lambda _: setW(W.value * 2))
    btns[3].on_click(lambda _: setW(W.value // 2))
    def use_period(_):
        if p:
            saltus.value = p.saltus
            setW(p.period)
        else:
            setW(s.best_lag())

    per_btn.on_click(use_period)

    def to_end(_):
        cs = max(1, min(cell.value, max(1, 4000 // W.value)))
        start.value = max(0, len(s) - (max_pixels // cs) * W.value)

    tail_btn.on_click(to_end)
    for c in (W, start, cell, mode, bit, saltus):
        c.observe(redraw, names="value")
    redraw()
    controls = w.VBox([
        w.HBox([W, Wsl, *btns, per_btn]),
        w.HBox([start, tail_btn, cell, mode, bit, saltus]),
        info,
    ])
    return w.VBox([w.HTML(f"<b>{s.name}</b> — {len(s):,} values, "
                          f"{p if p else 'no period found'}"), controls,
                   w.Box([img], layout=w.Layout(overflow="auto", max_height="85vh"))])


NimSequence.explore = explore
