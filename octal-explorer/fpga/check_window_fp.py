#!/usr/bin/env python3
"""Check window_fp.log from tb_window_fp: every fingerprint against the window
recomputed from scratch (Horner, sampled) and against the rolling update (all).
    check_window_fp.py LOG [DEPTH]"""
import sys

PLOW = 0x1028ecf30eb3f40d
M64 = (1 << 64) - 1


def mulx9(h):
    for _ in range(9):
        h = ((h << 1) & M64) ^ (PLOW if h >> 63 else 0)
    return h


def mulmod(a, b):
    r = 0
    for i in reversed(range(64)):
        r = ((r << 1) & M64) ^ (PLOW if r >> 63 else 0)
        if b >> i & 1:
            r ^= a
    return r


def powx(e):
    r, b = 1, 2
    while e:
        if e & 1:
            r = mulmod(r, b)
        b = mulmod(b, b)
        e >>= 1
    return r


def window_fp(win):
    """win oldest first; zeros before the stream (the ring's power-up contents)."""
    h = 0
    for v in win:
        h = mulx9(h) ^ v
    return h


def main():
    log, depth = sys.argv[1], int(sys.argv[2]) if len(sys.argv) > 2 else 20627
    rows = [tuple(int(t, 16) for t in line.split()) for line in open(log)]
    vals = [0] * depth + [v for v, _ in rows]       # the zeros the ring starts with
    bad = checked = 0
    # from scratch at ~60 points, including the first events and every window boundary
    pts = sorted(set(list(range(0, 5)) + list(range(0, len(rows), len(rows) // 50))
                     + [k * depth + d for k in (1, 2, 3) for d in (-1, 0, 1)] + [len(rows) - 1]))
    for i in pts:
        if 0 <= i < len(rows):
            exp = window_fp(vals[i + 1:i + 1 + depth])
            checked += 1
            if exp != rows[i][1]:
                bad += 1
                if bad <= 5:
                    print(f"event {i}: fp {rows[i][1]:016x}, expected {exp:016x}")
    # every event: the rolling update fp' = fp x^9 + v + old x^(9D)
    T = [powx(9 * depth + i) for i in range(9)]
    h = 0
    for i, (v, got) in enumerate(rows):
        old = vals[i]                               # leaves when value i enters
        h = mulx9(h) ^ v
        for b in range(9):
            if old >> b & 1:
                h ^= T[b]
        if h != got:
            bad += 1
            if bad <= 5:
                print(f"event {i}: fp {got:016x}, rolling {h:016x}")
    print(f"{len(rows)} events all checked (rolling), {checked} from scratch, {bad} mismatches")
    sys.exit(1 if bad or not rows else 0)


if __name__ == "__main__":
    main()
