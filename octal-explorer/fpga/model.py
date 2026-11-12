#!/usr/bin/env python3
"""Cycle-accurate Python model of officers_core (same taps, stages and clock enable).

xsim needs minutes for the 20,000-cycle preload of the full core, so this model
is the quick check of the generator's pipeline arithmetic; sim.sh is the check
of the RTL itself.

    model.py PARAMS_JSON VALUES_TXT N0 NRUN [--stall PCT]
"""
import argparse
import json
import random
import re

MASK = 0x1EE


def common(v):
    return bin(v & MASK).count("1") & 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("params")
    ap.add_argument("values")
    ap.add_argument("n0", type=int)
    ap.add_argument("nrun", type=int)
    ap.add_argument("--stall", type=int, default=20)
    args = ap.parse_args()
    P = json.load(open(args.params))
    G = [int(t) for t in re.findall(r"\d+", open(args.values).read())]
    T, depth, pre = P["tree_stages"], P["depth"], P["preload"]
    rare = list(zip(P["rare"][1:], P["rare_values"][1:]))       # a >= 1
    tree = [(a, r) for a, r in rare if a >= T]
    late = [(a, r) for a, r in rare if a < T]

    H = [None] * depth            # None = never loaded (no marks)
    stages = [set() for _ in range(T - 1)]   # L1 .. L(T-1)
    M0 = set()

    def marks(taps, off):
        s = set()
        for a, r in taps:
            g = H[a - off]
            if g is not None and common(g):
                s.add(r ^ g)       # common, since r is rare
        return s

    rnd = random.Random(1)
    i = got = bad = 0
    while got < args.nrun:
        loading = i < pre
        if loading and rnd.randrange(100) < 10:
            continue                              # gap: no clock enable
        if not loading and rnd.randrange(100) < args.stall:
            continue                              # stall: no clock enable
        # combinational loop: first free common value of M0 | {G(n-1)}
        own = {H[0]} if H[0] is not None and common(H[0]) else set()
        v = 0
        while not common(v) or v in M0 or v in own:
            v += 1
        # clock edge (all registers from the old state)
        new_M0 = stages[-1] | marks(late, 1)
        new_stages = [marks(tree, T)] + stages[:-1]
        new_H0 = G[args.n0 - pre + i] if loading else v
        if not loading:
            exp = G[args.n0 + got]
            if v != exp:
                if bad < 10:
                    print(f"MISMATCH #{got}: got {v} expected {exp}")
                bad += 1
            got += 1
        else:
            i += 1
        M0, stages = new_M0, new_stages
        H = [new_H0] + H[:-1]
    print(f"MODEL {got} values, {bad} mismatches")


if __name__ == "__main__":
    main()
