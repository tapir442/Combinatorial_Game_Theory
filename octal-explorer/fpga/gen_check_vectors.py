#!/usr/bin/env python3
"""Test vectors for check_group_lr.sv: G(0 .. N-1) and the witness depth of heaps.

    gen_check_vectors.py OUTDIR N0 HEAPS

Writes OUTDIR/g.hex (G(0 .. N0+HEAPS-1), one hex value per line, for $readmemh)
and OUTDIR/expect.txt ("n g depth" for n = N0 .. N0+HEAPS-1).

G comes from octal --rare (as in run_board.py).  The depth is computed here the
way split_model.c does it: heap n, m = n - 1, scan a = 1, 2, ... and stop when
every rare-class value below g has been some G(a) ^ G(m - a); depth = that a.
N0 must be >= 100,000 so that check_group_lr's group-wide rare-G(a) shortcut
holds (its header).
"""
import os
import subprocess
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))


def is_rare(v):
    """Rare class of .6: popcount(v & 0x1EE) even (works on numpy arrays too)."""
    p = v & 0x1EE
    p = p ^ (p >> 4)
    p = p ^ (p >> 2)
    p = p ^ (p >> 1)
    p = p ^ (p >> 8)
    return (p & 1) == 0


def main():
    out, n0, heaps = sys.argv[1], int(float(sys.argv[2])), int(sys.argv[3])
    assert n0 >= 100_000, "N0 must be >= 100000 (see check_group_lr.sv)"
    n = n0 + heaps
    res = subprocess.run([os.path.join(HERE, "..", "octal"), ".6", "-n", str(n), "-p", "--rare", "-q"],
                         capture_output=True, text=True, check=True)
    G = np.array([int(t) for t in res.stdout.split()], dtype=np.int32)
    assert len(G) == n, (len(G), n)
    os.makedirs(out, exist_ok=True)
    with open(os.path.join(out, "g.hex"), "w") as f:
        f.write("\n".join(f"{v:03x}" for v in G) + "\n")
    with open(os.path.join(out, "expect.txt"), "w") as f:
        for k in range(n0, n):
            m, g = k - 1, int(G[k])
            a = np.arange(1, m // 2 + 1)
            x = G[a] ^ G[m - a]
            sel = is_rare(x) & (x < g)
            vals, first = np.unique(x[sel], return_index=True)
            need = sum(1 for v in range(g) if is_rare(v))
            assert len(vals) == need, f"heap {k}: only {len(vals)} of {need} rare values below {g}"
            depth = int(a[sel][first].max())
            f.write(f"{k} {g} {depth}\n")
    print(f"wrote {n} values and {heaps} heaps from n = {n0} to {out}")


if __name__ == "__main__":
    main()
