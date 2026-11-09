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
