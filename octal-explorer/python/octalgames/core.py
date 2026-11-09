"""NimSequence: the nim-sequence of a take-and-break game, backed by the C code."""
from __future__ import annotations

import colorsys
import html
import sys
import time
from dataclasses import dataclass
from functools import lru_cache
from typing import Iterable, Optional

import numpy as np

from . import _lib


@dataclass(frozen=True)
class Period:
    """Eventual period of a nim-sequence.

    ``proven`` is True when the octal periodicity theorem certifies it
    (G(n+p) = G(n) for i <= n < 2i + p + t, t = last code digit).  Otherwise the
    period is only *apparent*: seen for at least three periods at the end of
    the computed range.
    """
    preperiod: int
    period: int
    proven: bool

    def __str__(self) -> str:
        kind = "proved" if self.proven else "apparent"
        return f"period {self.period}, preperiod {self.preperiod} ({kind})"


# --------------------------------------------------------------- colours

@lru_cache(maxsize=None)
def _palette_rgb(v: int) -> tuple:
    if v == 0:
        return (25, 25, 35)
    r, g, b = colorsys.hsv_to_rgb((v * 0.6180339887) % 1.0, 0.70, 0.92)
    return (int(255 * r + 0.5), int(255 * g + 0.5), int(255 * b + 0.5))


def palette(max_value: int) -> np.ndarray:
    """RGB lookup table (max_value+1, 3), uint8 — same colours as the C program."""
    return np.array([_palette_rgb(v) for v in range(int(max_value) + 1)], dtype=np.uint8)


def colormap(max_value: int):
    """Matplotlib ListedColormap with one colour per value 0..max_value."""
    from matplotlib.colors import ListedColormap
    return ListedColormap(palette(max_value) / 255.0, name="nimvalues")


# --------------------------------------------------------------- progress

class _Progress:
    def __init__(self, total: int, done: int, label: str):
        self.total, self.label, self.bar = total, label, None
        self.t0 = time.time()
        try:
            from IPython import get_ipython
            if get_ipython() is not None and "IPKernelApp" in get_ipython().config:
                import ipywidgets as w
                from IPython.display import display
                self.bar = w.IntProgress(min=0, max=total, value=done, description=label,
                                         layout=w.Layout(width="60%"))
                self.text = w.Label()
                self.box = w.HBox([self.bar, self.text])
                display(self.box)
        except Exception:
            self.bar = None

    def update(self, done: int, maxv: int) -> None:
        msg = f"n = {done:,} / {self.total:,}   max value {maxv}   {time.time() - self.t0:.1f} s"
        if self.bar is not None:
            self.bar.value = done
            self.text.value = msg
        else:
            print(f"\r{self.label}: {msg}", end="", file=sys.stderr, flush=True)

    def close(self) -> None:
        if self.bar is not None:
            self.box.close()
        else:
            print(file=sys.stderr)


# --------------------------------------------------------------- sequence

class NimSequence:
    """Nim-values G(0), G(1), ... of an octal/hexadecimal game.

    >>> s = NimSequence(".77", 200)       # Kayles
    >>> s.period
    Period(preperiod=71, period=12, proven=True)
    """

    def __init__(self, code: str, n: int = 0, *, progress: bool = True, chunk_seconds: float = 0.3):
        self.code = code
        self.name = _lib.rule_name(code)
        self._G = np.zeros(0, dtype=np.uint16)
        self.seconds = 0.0
        self._period_cache = None
        if n:
            self.extend(n, progress=progress, chunk_seconds=chunk_seconds)

    # ---- data access
    @property
    def values(self) -> np.ndarray:
        """Read-only numpy view of the computed values."""
        v = self._G.view()
        v.flags.writeable = False
        return v

    def __len__(self) -> int:
        return len(self._G)

    def __getitem__(self, i):
        return self.values[i]

    def __iter__(self):
        return iter(self.values.tolist())

    @property
    def max_value(self) -> int:
        return int(self._G.max()) if len(self._G) else 0

    # ---- computation
    def extend(self, n: int, *, progress: bool = True, chunk_seconds: float = 0.3) -> "NimSequence":
        """Compute values up to heap size n-1 (keeps what is already there).

        Runs in chunks, so a Jupyter interrupt (Kernel > Interrupt) stops the
        computation cleanly and keeps the values computed so far.

        Parameters
        ----------
        n
            New total length.  Nothing happens if ``n <= len(self)``.
        progress
            Show a progress bar (an ipywidgets bar in Jupyter, otherwise text on
            stderr) when more than 20,000 values are computed.
        chunk_seconds
            Target duration of one C call.  Smaller values make interrupts more
            responsive.

        Returns
        -------
        NimSequence
            ``self``, so calls can be chained.
        """
        start = len(self._G)
        if n <= start:
            return self
        G = np.zeros(n, dtype=np.uint16)
        G[:start] = self._G
        done, chunk = start, 256
        bar = _Progress(n, start, self.code) if progress and n - start > 20000 else None
        t_all = time.perf_counter()
        try:
            while done < n:
                stop = min(n, done + chunk)
                t = time.perf_counter()
                _lib.extend(self.code, G, done, stop)
                dt = time.perf_counter() - t
                done = stop
                # adapt chunk size: cost per value grows linearly with n
                chunk = max(64, int(chunk * min(4.0, chunk_seconds / max(dt, 1e-4))))
                if bar:
                    bar.update(done, int(G[:done].max()))
        except KeyboardInterrupt:
            print(f"\ninterrupted: kept {done:,} values", file=sys.stderr)
        finally:
            if bar:
                bar.close()
            self._G = G[:done].copy()
            self.seconds += time.perf_counter() - t_all
            self._period_cache = None
        return self

    # ---- analysis
    @property
    def period(self) -> Optional[Period]:
        """Eventual period (proved or apparent), or None."""
        if self._period_cache is None:
            rc, pre, per = _lib.period(self.code, self._G, len(self._G))
            self._period_cache = (Period(pre, per, rc == 2),) if rc else (None,)
        return self._period_cache[0]

    def records(self):
        """List of (n, value) where a new maximum value first appears."""
        G = self._G.astype(np.int64)
        if not len(G):
            return []
        cm = np.maximum.accumulate(G)
        idx = np.flatnonzero(np.r_[True, cm[1:] > cm[:-1]])
        return [(int(i), int(G[i])) for i in idx]

    def counts(self) -> np.ndarray:
        """counts[v] = number of heap sizes with value v."""
        return np.bincount(self._G, minlength=self.max_value + 1)

    def autocorr(self, max_lag: int = 1000, tail: float = 0.5) -> np.ndarray:
        """a[p] = fraction of n in the last `tail` part with G(n) = G(n+p); a[0] = 1."""
        G = self._G
        N = len(G)
        L = max(1, min(max_lag, N // 3))
        s = int(N * (1 - tail))
        a = np.ones(L + 1)
        for p in range(1, L + 1):
            a[p] = np.count_nonzero(G[s:N - p] == G[s + p:N]) / max(1, N - p - s)
        return a

    def best_lag(self, max_lag: int = 1000) -> int:
        a = self.autocorr(max_lag)
        return int(np.flatnonzero(a[1:] >= a[1:].max() - 1e-12)[0] + 1)

    def p_positions(self) -> np.ndarray:
        """Heap sizes with G(n) = 0."""
        return np.flatnonzero(self._G == 0)

    def first_occurrence(self) -> np.ndarray:
        """first[v] = smallest n with G(n) = v, or -1 if v does not occur."""
        first = np.full(self.max_value + 1, -1, dtype=np.int64)
        vals, idx = np.unique(self._G, return_index=True)
        first[vals] = idx
        return first

    def wrap(self, width: int, start: int = 0, rows: Optional[int] = None) -> np.ndarray:
        """2-D array: row r, column c holds G(start + r*width + c); -1 past the end."""
        G = self._G
        avail = max(0, len(G) - start)
        nrows = -(-avail // width) if rows is None else rows
        out = np.full(nrows * width, -1, dtype=np.int32)
        k = min(avail, nrows * width)
        out[:k] = G[start:start + k]
        return out.reshape(nrows, width)

    def image(self, width: int, mode: str = "values", *, start: int = 0, rows: Optional[int] = None,
              bit: int = 0, threshold: int = 1, cell: int = 1) -> np.ndarray:
        """RGB image (uint8, shape ``(rows*cell, width*cell, 3)``) of the wrapped sequence.

        Parameters
        ----------
        width
            Values per row.  Row r, column c shows G(start + r*width + c).
        mode
            ``'values'``: palette colour per value (0 is near-black).
            ``'diff'``: dark red where G(n) != G(n - width), grey in the first row.
            ``'zero'``: P-positions (G = 0) black.
            ``'bit'``: bit ``bit`` of G(n) black.
            ``'ge'``: only values >= ``threshold`` coloured.
        start
            First heap size shown.
        rows
            Number of rows (default: all remaining values).
        bit
            Bit index for ``mode='bit'``.
        threshold
            Threshold for ``mode='ge'``.
        cell
            Pixel size of one value (nearest-neighbour upscaling).
        """
        A = self.wrap(width, start, rows)
        H, W = A.shape
        img = np.full((H, W, 3), 255, dtype=np.uint8)
        valid = A >= 0
        if mode == "values":
            lut = palette(max(self.max_value, 1))
            img[valid] = lut[A[valid]]
        elif mode == "diff":
            n = start + np.arange(H * W).reshape(H, W)
            prev = np.where(n - width >= 0, n - width, 0)
            G = self._G
            ok = valid & (n >= width)
            same = np.zeros_like(valid)
            same[ok] = G[n[ok]] == G[prev[ok]]
            img[valid & (n < width)] = (200, 200, 200)
            img[ok & same] = (248, 248, 245)
            img[ok & ~same] = (150, 20, 30)
        elif mode == "zero":
            img[valid] = (245, 245, 245)
            img[valid & (A == 0)] = (20, 20, 20)
        elif mode == "bit":
            img[valid] = (245, 245, 245)
            img[valid & ((A >> bit) & 1 == 1)] = (20, 20, 20)
        elif mode == "ge":
            lut = palette(max(self.max_value, 1))
            img[valid] = (245, 245, 245)
            m = valid & (A >= threshold)
            img[m] = lut[A[m]]
        else:
            raise ValueError("mode must be one of values, diff, zero, bit, ge")
        if cell > 1:
            img = img.repeat(cell, axis=0).repeat(cell, axis=1)
        return img

    # ---- export
    def to_frame(self):
        import pandas as pd
        return pd.DataFrame({"n": np.arange(len(self._G)), "G": self._G})

    def save_csv(self, path) -> None:
        np.savetxt(path, np.c_[np.arange(len(self._G)), self._G], fmt="%d", delimiter=",",
                   header="n,G", comments="")

    @classmethod
    def from_csv(cls, code: str, path) -> "NimSequence":
        """Load values saved with save_csv (resume with .extend)."""
        data = np.loadtxt(path, delimiter=",", skiprows=1, dtype=np.int64)
        s = cls(code)
        s._G = data[:, 1].astype(np.uint16)
        return s

    def summary(self) -> dict:
        p = self.period
        mx = self.max_value
        return {
            "code": self.code, "name": self.name, "n": len(self),
            "period": p.period if p else None, "preperiod": p.preperiod if p else None,
            "proven": p.proven if p else False, "max_value": mx,
            "max_at": int(np.argmax(self._G)) if len(self) else None,
            "p_positions": int(np.count_nonzero(self._G == 0)),
            "seconds": round(self.seconds, 3),
        }

    # ---- display
    def __repr__(self) -> str:
        p = self.period
        return f"<NimSequence {self.code}: n={len(self):,}, max={self.max_value}, {p if p else 'no period found'}>"

    def _repr_html_(self) -> str:
        s = self.summary()
        p = self.period
        head = " ".join(str(int(v)) for v in self._G[:60])
        rows = [
            ("Game", html.escape(self.name)),
            ("Heap sizes", f"0 … {s['n'] - 1:,}"),
            ("Periodicity", html.escape(str(p)) if p else "no period found"),
            ("Largest value", f"{s['max_value']} (first at n = {s['max_at']})"),
            ("P-positions", f"{s['p_positions']:,}"),
            ("First values", f"<code>{head}{' …' if len(self) > 60 else ''}</code>"),
        ]
        body = "".join(f"<tr><th style='text-align:left;padding-right:1em'>{k}</th><td style='text-align:left'>{v}</td></tr>"
                       for k, v in rows)
        return f"<table>{body}</table>"

    # plotting / widgets are attached in plot.py and widgets.py


def compute(code: str, n: int, **kw) -> NimSequence:
    """Compute G(0), ..., G(n-1) for a game.

    Parameters
    ----------
    code
        Game code: ``'.007'``, ``'0.6'``, ``'.137'``, ``'4.'``, ``'.f6'``, or the
        named rules ``'grundy'`` and ``'couples'``.  See the *Concepts* page.
    n
        Number of heap sizes.
    **kw
        Passed to :meth:`NimSequence.extend` (``progress``, ``chunk_seconds``).

    Returns
    -------
    NimSequence

    Examples
    --------
    >>> import octalgames as og
    >>> og.compute(".77", 300).period          # Kayles
    Period(preperiod=71, period=12, proven=True)
    """
    return NimSequence(code, n, **kw)


def octal_codes(digits: int, *, max_digit: int = 7, include_d0: bool = False) -> list:
    """List the game codes with a given number of digits.

    Parameters
    ----------
    digits
        Number of digits after the point.  The last digit is always nonzero.
    max_digit
        Largest digit: 7 for octal games, 15 for hexadecimal games.
    include_d0
        Also return variants with a digit before the point (4, plus 8 and c in hex).

    Examples
    --------
    >>> og.octal_codes(1)
    ['.1', '.2', '.3', '.4', '.5', '.6', '.7']
    """
    sym = "0123456789abcdef"[: max_digit + 1]
    out = [""]
    for _ in range(digits):
        out = [o + c for o in out for c in sym]
    codes = ["." + o for o in out if o[-1] != "0"]
    if include_d0:
        codes += [d + c for d in "48c"[: 1 + (max_digit > 7) * 2] for c in codes]
    return codes


def survey(codes: Iterable[str], n: int = 5000, *, progress: bool = False):
    """Compute many games and return a pandas DataFrame of their summaries.

    Parameters
    ----------
    codes
        Game codes, for example ``og.octal_codes(2)``.
    n
        Heap sizes per game.
    progress
        Show a progress bar per game.

    Returns
    -------
    pandas.DataFrame
        Indexed by code, with the columns of :meth:`NimSequence.summary`:
        ``name, n, period, preperiod, proven, max_value, max_at, p_positions, seconds``.
    """
    import pandas as pd
    rows = []
    for c in codes:
        rows.append(NimSequence(c, n, progress=progress).summary())
    return pd.DataFrame(rows).set_index("code")
