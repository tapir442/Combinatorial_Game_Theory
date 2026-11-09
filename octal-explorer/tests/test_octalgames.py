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
