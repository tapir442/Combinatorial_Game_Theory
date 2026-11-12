#!/bin/sh
# Simulate the folded core (officers_folded) with xsim against octal.
#   fpga/sim_folded.sh [N0] [NRUN] [FOLD] [STALL%]     (defaults 50000, 2000, 8, 20)
set -e
here=$(cd "$(dirname "$0")" && pwd)
n0=${1:-50000}; nrun=${2:-2000}; fold=${3:-8}; stall=${4:-20}
b="$here/build/sim_folded"
mkdir -p "$b"
"$here/../octal" .6 -n $((n0 + nrun)) -p --rare -q > "$b/values.txt"
"$here/../octal" .6 -n 100000 -p --rare -q > "$b/values-table.txt"
python3 "$here/gen_folded.py" --values "$b/values-table.txt" --fold "$fold" -o "$b"
pre=$(python3 -c "import json;print(json.load(open('$b/folded_params.json'))['preload'])")
python3 - "$b" "$n0" "$nrun" "$pre" <<'PY'
import re, sys
b, n0, nrun, pre = sys.argv[1], *map(int, sys.argv[2:])
G = [int(t) for t in re.findall(r"\d+", open(f"{b}/values.txt").read())]
open(f"{b}/preload.hex", "w").write("".join(f"{v:03x}\n" for v in G[n0 - pre:n0]))
open(f"{b}/expected.hex", "w").write("".join(f"{v:03x}\n" for v in G[n0:n0 + nrun]))
PY
cd "$b"
if ! ldconfig -p | grep -q 'libncurses.so.5 '; then
    mkdir -p compat
    ln -sf /usr/lib/x86_64-linux-gnu/libncurses.so.6 compat/libncurses.so.5
    ln -sf /usr/lib/x86_64-linux-gnu/libtinfo.so.6 compat/libtinfo.so.5
    export LD_LIBRARY_PATH="$b/compat${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
fi
xvlog -sv officers_folded.sv "$here/ffs256.sv" "$here/tb_folded.sv" > xvlog.out
xelab -O3 tb_folded -generic_top "PRELOAD=$pre" -generic_top "NRUN=$nrun" -generic_top "STALL=$stall" -s tb > xelab.out
xsim tb -runall | grep -E "RESULT|MISMATCH|Error|Fatal"
