#!/usr/bin/env python3
"""Write test vectors for the xsim testbenches.

    make_vectors.py GEN_DIR N0 NRUN

Reads GEN_DIR/values.txt (octal -p output) and the params JSON files of the
generated cores, then writes GEN_DIR/sim_folded/ and GEN_DIR/sim_full/, each
with preload.hex (G(n0 - PRELOAD .. n0-1)) and expected.hex (G(n0 ..
n0+NRUN-1)).  The two cores take different preload lengths.
"""
import json
import os
import re
import sys

gen, n0, nrun = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
G = [int(t) for t in re.findall(r"\d+", open(os.path.join(gen, "values.txt")).read())]
for name, pj in (("folded", "folded_params.json"), ("full", "officers_params.json")):
    pj = os.path.join(gen, pj)
    if not os.path.exists(pj):
        continue
    P = json.load(open(pj))
    pre = P["preload"]
    if n0 < max(pre, P["min_n0"]):
        sys.exit(f"{name}: N0 must be >= {max(pre, P['min_n0'])}")
    d = os.path.join(gen, "sim_" + name)
    os.makedirs(d, exist_ok=True)
    open(os.path.join(d, "preload.hex"), "w").write("".join(f"{v:03x}\n" for v in G[n0 - pre:n0]))
    open(os.path.join(d, "expected.hex"), "w").write("".join(f"{v:03x}\n" for v in G[n0:n0 + nrun]))
    print(f"{d}: preload {pre}, expected {nrun}")
