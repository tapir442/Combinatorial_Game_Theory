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
    """Compile with OpenMP (method="blocked") and Vulkan (method="gpu", if the generated
    octal_gpu_spv.h exists); drop what the system does not support."""
    cc = os.environ.get("CC", "cc")
    base = [cc, "-O3", "-march=native", "-std=c11", "-Wno-unused-function", "-shared", "-fPIC",
            "-DOCTAL_NO_MAIN", "-o", str(out), str(src), "-lz", "-lm"]
    omp, vk = ["-fopenmp"], ["-DOCTAL_VULKAN", "-lvulkan"]
    variants = [base[:1] + omp + base[1:], base]
    if src.with_name("octal_gpu_spv.h").exists():
        variants = [v + vk for v in variants] + variants
    for cmd in variants:
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode == 0:
            return
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
        deps = [src.with_name("octal.h"), src.with_name("octal_gpu_spv.h")]
        newest = max([src.stat().st_mtime] + [d.stat().st_mtime for d in deps if d.exists()])
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
    lib.octal_extend_blocked.argtypes = [ctypes.c_char_p, u16, ctypes.c_long, ctypes.c_long, ctypes.c_int]
    lib.octal_extend_blocked.restype = ctypes.c_int
    lib.octal_extend_gpu.argtypes = [ctypes.c_char_p, u16, ctypes.c_long, ctypes.c_long]
    lib.octal_extend_gpu.restype = ctypes.c_int
    lib.octal_gpu_name.argtypes = [ctypes.c_char_p, ctypes.c_int]
    lib.octal_gpu_name.restype = ctypes.c_int
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


METHODS = ("brute", "blocked", "gpu")


def gpu_name():
    """Name of the GPU used by method="gpu", or None if there is none (or no Vulkan support)."""
    buf = ctypes.create_string_buffer(256)
    if load().octal_gpu_name(buf, 256) != 0:
        return None
    return buf.value.decode()


def extend(code: str, values: np.ndarray, start: int, stop: int, method: str = "brute",
           threads: int = 0) -> None:
    """Fill values[start:stop] in place (values[:start] must be valid).

    method="brute" uses octal_extend() (one core); method="blocked" uses
    octal_extend_blocked() with `threads` threads (0 = all cores); method="gpu"
    uses octal_extend_gpu() (Vulkan).  All give the same values.  Thread-safe;
    releases the GIL while the C code runs.
    """
    if method == "brute":
        rc = load().octal_extend(code.encode(), values, start, stop)
    elif method == "blocked":
        rc = load().octal_extend_blocked(code.encode(), values, start, stop, int(threads))
    elif method == "gpu":
        rc = load().octal_extend_gpu(code.encode(), values, start, stop)
    else:
        raise ValueError(f"method must be one of {METHODS}, not {method!r}")
    if rc == -1:
        raise ValueError(f"cannot parse game code {code!r}")
    if rc == -2:
        raise OverflowError("nim-value exceeds 65535")
    if rc == -3:
        raise MemoryError("octal backend: out of memory")
    if rc == -4:
        raise RuntimeError("method='gpu': no usable Vulkan GPU, or the library was built without "
                           "Vulkan (needs octal_gpu_spv.h and libvulkan)")


def period(code: str, values: np.ndarray, n: int):
    """(rc, preperiod, period, saltus, proof_period, proof_saltus).

    rc: 0 none, 1 apparent, 2 proved by the octal periodicity theorem,
    3 proved by Austin's Theorem 6.8.
    """
    out = (ctypes.c_long * 5)()
    rc = load().octal_period_ex(code.encode(), values, n, out)
    if rc < 0:
        raise ValueError(f"cannot parse game code {code!r}")
    return rc, out[0], out[1], out[2], out[3], out[4]
