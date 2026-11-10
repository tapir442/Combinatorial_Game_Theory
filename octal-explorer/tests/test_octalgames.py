import numpy as np
import pytest

import octalgames as og


def brute(code_digits, n):
    """Pure-Python reference for octal codes .d1d2... (no d0, digits <= 7)."""
    G = []
    for h in range(n):
        opts = set()
        for k, d in enumerate(code_digits, start=1):
            m = h - k
            if m < 0:
                break
            if d & 1 and m == 0:
                opts.add(0)
            if d & 2 and m >= 1:
                opts.add(G[m])
            if d & 4:
                for a in range(1, m // 2 + 1):
                    opts.add(G[a] ^ G[m - a])
        v = 0
        while v in opts:
            v += 1
        G.append(v)
    return G


@pytest.mark.parametrize("code", [".07", ".77", ".6", ".007", ".137", ".164", ".4"])
def test_matches_python_reference(code):
    digits = [int(c) for c in code[1:]]
    s = og.compute(code, 400, progress=False)
    assert s.values.tolist() == brute(digits, 400)


def test_kayles_period_proved():
    p = og.compute(".77", 300, progress=False).period
    assert (p.period, p.preperiod, p.proven) == (12, 71, True)


def test_dawsons_kayles_period_proved():
    p = og.compute(".07", 300, progress=False).period
    assert (p.period, p.preperiod, p.proven) == (34, 53, True)


def test_extend_equals_one_shot():
    a = og.compute(".007", 3000, progress=False)
    b = og.compute(".007", 1000, progress=False).extend(3000, progress=False)
    assert np.array_equal(a.values, b.values)


def test_grundy_start():
    # Grundy's game, WW: 0 0 0 1 0 2 1 0 2 1 0 2 1 3 2 1 3 2 4 3 0
    s = og.compute("grundy", 21, progress=False)
    assert s.values.tolist() == [0, 0, 0, 1, 0, 2, 1, 0, 2, 1, 0, 2, 1, 3, 2, 1, 3, 2, 4, 3, 0]


def test_bad_code():
    with pytest.raises(ValueError):
        og.compute("xyz", 10)


def test_wrap_and_image_shapes():
    s = og.compute(".77", 100, progress=False)
    assert s.wrap(12).shape == (9, 12)
    assert s.wrap(12)[-1, -1] == -1
    for mode in ("values", "diff", "zero", "bit", "ge"):
        assert s.image(12, mode, cell=2).shape == (18, 24, 3)


def test_survey():
    df = og.survey([".07", ".77"], 300)
    assert df.loc[".77", "period"] == 12


def test_threads_parallel_equal_sequential():
    """Several games computed in parallel threads give the sequential results."""
    from concurrent.futures import ThreadPoolExecutor

    codes = [".007", ".6", ".77", ".07", "grundy", "couples", ".137", ".164"]
    seq = {c: og.compute(c, 3000, progress=False).values.copy() for c in codes}
    with ThreadPoolExecutor(max_workers=8) as ex:
        par = dict(zip(codes, ex.map(lambda c: og.compute(c, 3000, progress=False, chunk_seconds=0.001).values, codes)))
    for c in codes:
        assert np.array_equal(seq[c], par[c]), c


def test_threads_give_speedup():
    """Without a global lock, threads overlap: 4 games in 4 threads take well under 4x one game."""
    import time
    from concurrent.futures import ThreadPoolExecutor

    n = 40000
    t = time.perf_counter()
    og.compute(".6", n, progress=False)
    one = time.perf_counter() - t
    t = time.perf_counter()
    with ThreadPoolExecutor(max_workers=4) as ex:
        list(ex.map(lambda c: og.compute(c, n, progress=False), [".6", ".007", ".07", ".137"]))
    four = time.perf_counter() - t
    assert four < 2.5 * one, (one, four)


def test_folded_frequency_and_profile():
    s = og.compute(".007", 20000, progress=False)
    F = s.folded_frequency(64)
    assert F.shape[1] == 64
    assert F.sum() == len(s)                       # every heap size counted once
    assert np.array_equal(F.ravel()[: s.max_value + 1], s.counts())
    N = s.folded_frequency(64, normalize=True)
    full = np.isfinite(N).all(axis=1)
    assert np.allclose(N[full].mean(axis=1), 1.0)  # rows normalised to mean 1
    prof = s.folded_profile(64)
    assert prof.shape == (64,) and np.isclose(np.nanmean(prof), 1.0)
    assert prof[:4].mean() > 1.0                   # block-start excess (see docs)


def test_plot_folded_kinds():
    import matplotlib
    matplotlib.use("Agg")
    s = og.compute(".007", 5000, progress=False)
    for kind in ("profile", "heatmap"):
        assert s.plot_folded(64, kind) is not None
    assert len(s.plot_folded(64, "both")) == 2


def test_arithmetic_periodicity_hexadecimal():
    # Nowakowski, Problem A3: .28 = .29 period 53 saltus 16 (only G(0) exceptional),
    # .9c period 36, preperiod 28, saltus 16.  All three satisfy Austin's Theorem 6.8.
    for code, exp in ((".28", (1, 53, 16)), (".29", (1, 53, 16)), (".9c", (28, 36, 16))):
        p = og.compute(code, 1500, progress=False).period
        assert (p.preperiod, p.period, p.saltus, p.proven, p.proof) == (*exp, True, "Austin 6.8"), (code, p)
    s = og.compute(".28", 1500, progress=False)
    img = s.image(53, "diff")                      # saltus taken from the detected period
    dark = np.argwhere((img == (150, 20, 30)).all(axis=2))
    assert dark.tolist() == [[1, 0]]               # only n = 53: G(53) - G(0) = 15, G(0) exceptional


def test_ordinary_periods_unchanged_and_f6_breaks():
    p = og.compute(".77", 300, progress=False).period
    assert (p.preperiod, p.period, p.saltus, p.proven, p.proof) == (71, 12, 0, True, "periodicity theorem")
    # .f6 looks like period 43, saltus 32 from 604 up to n ~ 1900, but not beyond
    p = og.compute(".f6", 1900, progress=False).period
    assert (p.preperiod, p.period, p.saltus) == (604, 43, 32)
    assert og.compute(".f6", 2500, progress=False).period is None


def test_austin_theorem_examples():
    """Austin's thesis (1976), Ch. 6: .B8 has last irregular value G(8) = 3, period 7,
    saltus 4, and needs 8 + 8*7 + 2 - 1 = 65 values; .F8 has period 6, saltus 4."""
    p = og.compute(".b8", 200, progress=False).period
    assert (p.preperiod, p.period, p.saltus, p.proof) == (9, 7, 4, "Austin 6.8")
    assert og.compute(".b8", 66, progress=False).period.proven          # exactly enough data
    q = og.compute(".b8", 65, progress=False).period
    assert q is None or not q.proven                                     # one value short
    p = og.compute(".f8", 200, progress=False).period
    assert (p.period, p.saltus, p.proven) == (6, 4, True)
    # saltus not a power of 2 -> Austin's theorem cannot apply: .3F is 012012(+3) (Kenyon)
    p = og.compute(".3f", 2000, progress=False).period
    assert (p.period, p.saltus, p.proven) == (6, 3, False)


@pytest.mark.parametrize("code", [".007", ".6", ".77", "grundy", "couples", "4.07", ".f6"])
def test_blocked_equals_brute(code):
    """The blocked multi-threaded method gives exactly the brute-force values,
    also when computed in many small chunks that cut through blocks."""
    n = 3000 if code == ".f6" else 30000
    a = og.compute(code, n, progress=False)
    b = og.compute(code, n, progress=False, method="blocked")
    c = og.compute(code, n, progress=False, method="blocked", threads=3, chunk_seconds=0.0005)
    assert np.array_equal(a.values, b.values)
    assert np.array_equal(a.values, c.values)


def test_blocked_extend_and_override():
    s = og.compute(".007", 10000, progress=False, method="blocked", threads=2)
    s.extend(20000, progress=False, method="brute")
    s.extend(25000, progress=False)
    assert s.method == "blocked"
    assert np.array_equal(s.values, og.compute(".007", 25000, progress=False).values)


def test_bad_method():
    with pytest.raises(ValueError):
        og.compute(".007", 10, method="cuda")


@pytest.mark.skipif(og.gpu_name() is None, reason="no Vulkan GPU / built without Vulkan")
@pytest.mark.parametrize("code", [".007", ".6", "grundy", "couples", "4.07"])
def test_gpu_equals_brute(code):
    n = 60000
    a = og.compute(code, n, progress=False)
    b = og.compute(code, n, progress=False, method="gpu", chunk_seconds=0.01)
    assert np.array_equal(a.values, b.values)


def test_gpu_unavailable_raises_or_works():
    if og.gpu_name() is None:
        with pytest.raises(RuntimeError):
            og.compute(".007", 40000, progress=False, method="gpu")
