"""ctypes binding to the C backend (octal.c compiled with -DOCTAL_NO_MAIN).

The shared library is looked up next to this file.  If it is missing, or if
octal.c or octal.h is newer than it, it is (re)compiled automatically with $CC (default cc).
Set OCTALGAMES_LIB to use a library somewhere else.
"""
from __future__ import annotations

import ctypes
import enum
import itertools
import os
import shutil
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


def _build_rocm(src: Path, out: Path):
    """Compile octal_rocm.hip with hipcc (for Method.ROCM); return the link flags, or []."""
    hip = src.with_name("octal_rocm.hip")
    hipcc = os.environ.get("HIPCC", "hipcc")
    if not hip.exists() or not shutil.which(hipcc):
        return []
    obj = out.with_name("octal_rocm.o")
    arch = os.environ.get("ROCM_ARCH", "native")
    # wave64: 1.8x faster than RDNA's default wave32 for this kernel
    r = subprocess.run([hipcc, "-O3", "-mwavefrontsize64", "-fPIC", f"--offload-arch={arch}", "-c", str(hip),
                        "-o", str(obj)],
                       capture_output=True, text=True)
    return [str(obj), "-DOCTAL_ROCM", "-lamdhip64", "-lstdc++"] if r.returncode == 0 else []


def _build(src: Path, out: Path) -> None:
    """Compile with OpenMP (Method.BLOCKED), Vulkan (Method.GPU, if the generated
    octal_gpu_spv.h exists) and ROCm (Method.ROCM, if hipcc works); drop what the
    system does not support."""
    cc = os.environ.get("CC", "cc")
    base = [cc, "-O3", "-march=native", "-std=c11", "-Wno-unused-function", "-shared", "-fPIC",
            "-DOCTAL_NO_MAIN", "-o", str(out), str(src), "-lz", "-lm"]
    omp = ["-fopenmp"]
    vk = ["-DOCTAL_VULKAN", "-lvulkan"] if src.with_name("octal_gpu_spv.h").exists() else []
    rocm = _build_rocm(src, out)
    # every combination of the optional features, most features first
    variants = []
    for use_rocm, use_vk, use_omp in itertools.product((1, 0), repeat=3):
        if (use_rocm and not rocm) or (use_vk and not vk):
            continue
        variants.append(base + (omp if use_omp else []) + (vk if use_vk else []) + (rocm if use_rocm else []))
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
        deps = [src.with_name(f) for f in ("octal.h", "octal_gpu_spv.h", "octal_rocm.hip")]
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
    lib.octal_extend_rocm.argtypes = [ctypes.c_char_p, u16, ctypes.c_long, ctypes.c_long]
    lib.octal_extend_rocm.restype = ctypes.c_int
    lib.octal_extend_rare.argtypes = [ctypes.c_char_p, u16, ctypes.c_long, ctypes.c_long, ctypes.c_int]
    lib.octal_extend_rare.restype = ctypes.c_int
    lib.octal_rocm_name.argtypes = [ctypes.c_char_p, ctypes.c_int]
    lib.octal_rocm_name.restype = ctypes.c_int
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


class Method(str, enum.Enum):
    """How nim-values are computed.  All methods give exactly the same values.

    Wherever a Method is expected, its string value (``"blocked"`` ...) works too.
    """
    BRUTE = "brute"      #: octal_extend(): brute force, one core
    BLOCKED = "blocked"  #: octal_extend_blocked(): blocked method, all cores (the default)
    GPU = "gpu"          #: octal_extend_gpu(): blocked method on a Vulkan GPU
    ROCM = "rocm"        #: octal_extend_rocm(): blocked method on an AMD GPU with ROCm (HIP)
    RARE = "rare"        #: octal_extend_rare(): rare-value algorithm (Grossman 2017), speculation on threads

    def __str__(self) -> str:
        return self.value

    @classmethod
    def parse(cls, method) -> "Method":
        """``method`` as a Method; ValueError if it is not one (or the value of one)."""
        try:
            return cls(method)
        except ValueError:
            raise ValueError(f"method must be one of {[m.value for m in cls]}, not {method!r}") from None


#: what -4 (no usable GPU) means for each GPU method
_NO_DEVICE = {
    Method.GPU: "method='gpu': no usable Vulkan GPU, or the library was built without Vulkan "
                "(needs octal_gpu_spv.h and libvulkan)",
    Method.ROCM: "method='rocm': no usable ROCm GPU, or the library was built without ROCm "
                 "(needs hipcc and libamdhip64)",
}


def gpu_name():
    """Name of the GPU used by Method.GPU, or None if there is none (or no Vulkan support)."""
    buf = ctypes.create_string_buffer(256)
    if load().octal_gpu_name(buf, 256) != 0:
        return None
    return buf.value.decode()


def rocm_name():
    """Name of the GPU used by Method.ROCM, or None if there is none (or no ROCm support)."""
    buf = ctypes.create_string_buffer(256)
    if load().octal_rocm_name(buf, 256) != 0:
        return None
    return buf.value.decode()


def extend(code: str, values: np.ndarray, start: int, stop: int, method: Method | str = Method.BRUTE,
           threads: int = 0) -> None:
    """Fill values[start:stop] in place (values[:start] must be valid).

    Uses the C function of `method` (see Method); `threads` is the thread count
    for Method.BLOCKED and Method.RARE (0 = all cores; 1 = no speculation for RARE).  Thread-safe; releases the GIL while the
    C code runs.
    """
    method = Method.parse(method)
    lib, c = load(), code.encode()
    if method is Method.BLOCKED:
        rc = lib.octal_extend_blocked(c, values, start, stop, int(threads))
    elif method is Method.RARE:
        rc = lib.octal_extend_rare(c, values, start, stop, int(threads))
    else:
        fn = {Method.BRUTE: lib.octal_extend, Method.GPU: lib.octal_extend_gpu,
              Method.ROCM: lib.octal_extend_rocm}[method]
        rc = fn(c, values, start, stop)
    if rc == -1:
        raise ValueError(f"cannot parse game code {code!r}")
    if rc == -2:
        raise OverflowError("nim-value exceeds 65535")
    if rc == -3:
        raise MemoryError("octal backend: out of memory")
    if rc == -4:
        raise RuntimeError(_NO_DEVICE[method])


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
