"""octalgames — explore nim-sequences of octal and hexadecimal games from Python/Jupyter.

The heavy lifting is done by the C code in ../octal.c, loaded through ctypes.

    import octalgames as og
    s = og.compute(".007", 100_000)   # Treblecross
    s                                  # summary table in Jupyter
    s.period                           # Period(...) or None
    s.plot_values(); s.plot_wrap(); s.plot_autocorr(); s.dashboard()
    s.explore()                        # interactive wrap explorer (ipywidgets)
    s.extend(200_000)                  # continue the computation
    og.survey(og.octal_codes(2), 2000) # DataFrame over all two-digit octal games
"""
from .core import NimSequence, Period, colormap, compute, octal_codes, palette, survey
from . import plot as _plot  # noqa: F401  (attaches plot methods)
from .plot import dashboard, plot_autocorr, plot_first_occurrence, plot_histogram, plot_values, plot_wrap
from ._lib import rule_name

try:  # widgets are optional
    from .widgets import explore
except Exception:  # pragma: no cover
    explore = None

__all__ = ["NimSequence", "Period", "compute", "survey", "octal_codes", "rule_name", "palette",
           "colormap", "plot_values", "plot_wrap", "plot_autocorr", "plot_histogram",
           "plot_first_occurrence", "dashboard", "explore"]
__version__ = "0.1.0"
