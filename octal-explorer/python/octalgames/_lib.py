"""ctypes binding to the C backend (octal.c compiled with -DOCTAL_NO_MAIN).

The shared library is looked up next to this file.  If it is missing, or if
octal.c or octal.h is newer than it, it is (re)compiled automatically with $CC (default cc).
Set OCTALGAMES_LIB to use a library somewhere else.
"""
from __future__ import annotations

import ctypes
import os
import subprocess
import threading
from pathlib import Path

import numpy as np

_HERE = Path(__file__).resolve().parent
_SOURCES = [_HERE.parent.parent / "octal.c", _HERE / "octal.c"]
_lib = None
# Only loading/building is serialised.  The C functions are reentrant and run
# without the GIL (ctypes releases it), so computations in threads run in parallel.
_load_lock = threading.Lock()


class BackendError(RuntimeError):
    pass


def _build(src: Path, out: Path) -> None:
    cc = os.environ.get("CC", "cc")
    cmd = [cc, "-O3", "-march=native", "-std=c11", "-Wno-unused-function", "-shared", "-fPIC",
           "-DOCTAL_NO_MAIN", "-o", str(out), str(src), "-lz", "-lm"]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        raise BackendError(f"building {out.name} failed:\n{' '.join(cmd)}\n{r.stderr}")


def load():
    global _lib
    if _lib is not None:
        return _lib
    with _load_lock:
        if _lib is None:
            _lib = _load()
    return _lib


def _load():
    path = Path(os.environ.get("OCTALGAMES_LIB", _HERE / "liboctal.so"))
    src = next((s for s in _SOURCES if s.exists()), None)
    if "OCTALGAMES_LIB" not in os.environ and src is not None:
        hdr = src.with_name("octal.h")
        newest = max(src.stat().st_mtime, hdr.stat().st_mtime if hdr.exists() else 0)
        if not path.exists() or newest > path.stat().st_mtime:
            _build(src, path)
    if not path.exists():
        raise BackendError(f"{path} not found and octal.c not available to build it")
    lib = ctypes.CDLL(str(path))
    u16 = np.ctypeslib.ndpointer(np.uint16, flags="C_CONTIGUOUS")
    lib.octal_rule_name.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_int]
    lib.octal_rule_name.restype = ctypes.c_int
    lib.octal_extend.argtypes = [ctypes.c_char_p, u16, ctypes.c_long, ctypes.c_long]
    lib.octal_extend.restype = ctypes.c_int
    lib.octal_period.argtypes = [ctypes.c_char_p, u16, ctypes.c_long, ctypes.POINTER(ctypes.c_long)]
    lib.octal_period.restype = ctypes.c_int
    lib.octal_period_ex.argtypes = [ctypes.c_char_p, u16, ctypes.c_long, ctypes.POINTER(ctypes.c_long)]
    lib.octal_period_ex.restype = ctypes.c_int
    return lib


def rule_name(code: str) -> str:
    buf = ctypes.create_string_buffer(256)
    if load().octal_rule_name(code.encode(), buf, 256) != 0:
        raise ValueError(f"cannot parse game code {code!r} (examples: '.007', '0.6', '4.', 'grundy')")
    return buf.value.decode()


def extend(code: str, values: np.ndarray, start: int, stop: int) -> None:
    """Fill values[start:stop] in place (values[:start] must be valid).

    Thread-safe; releases the GIL while the C code runs.
    """
    rc = load().octal_extend(code.encode(), values, start, stop)
    if rc == -1:
        raise ValueError(f"cannot parse game code {code!r}")
    if rc == -2:
        raise OverflowError("nim-value exceeds 65535")
    if rc == -3:
        raise MemoryError("octal backend: out of memory")


def period(code: str, values: np.ndarray, n: int):
    """(rc, preperiod, period, saltus); rc 0 none, 1 apparent, 2 proved."""
    out = (ctypes.c_long * 3)()
    rc = load().octal_period_ex(code.encode(), values, n, out)
    if rc < 0:
        raise ValueError(f"cannot parse game code {code!r}")
    return rc, out[0], out[1], out[2]
