#!/bin/sh
# Simulate officers_core with the Vivado simulator (xsim) against octal.
#   fpga/sim.sh [N0] [NRUN]      (defaults 50000 and 10000)
# Needs Vivado in PATH (source .../Vivado/settings64.sh) and ../octal built.
set -e
here=$(cd "$(dirname "$0")" && pwd)
n0=${1:-50000}
nrun=${2:-10000}
b="$here/build"
mkdir -p "$b"
total=$((n0 + nrun))
"$here/../octal" .6 -n "$total" -p --rare -q > "$b/values.txt"
"$here/../octal" .6 -n 100000 -p --rare -q > "$b/values-table.txt"
python3 "$here/gen_officers.py" --values "$b/values-table.txt" -o "$b"
preload=$(python3 -c "import json;print(json.load(open('$b/officers_params.json'))['preload'])")
min_n0=$(python3 -c "import json;print(json.load(open('$b/officers_params.json'))['min_n0'])")
[ "$n0" -ge "$min_n0" ] || { echo "N0 must be >= $min_n0"; exit 1; }
python3 - "$b" "$n0" "$nrun" "$preload" <<'EOF'
import re, sys
b, n0, nrun, pre = sys.argv[1], *map(int, sys.argv[2:])
G = [int(t) for t in re.findall(r"\d+", open(f"{b}/values.txt").read())]
open(f"{b}/preload.hex", "w").write("".join(f"{v:03x}\n" for v in G[n0 - pre:n0]))
open(f"{b}/expected.hex", "w").write("".join(f"{v:03x}\n" for v in G[n0:n0 + nrun]))
EOF
cd "$b"
# xsim links against libncurses.so.5 / libtinfo.so.5, which newer distributions
# lack; the .so.6 ABI works for it, so point a private directory at them.
if ! ldconfig -p | grep -q 'libncurses.so.5 '; then
    mkdir -p compat
    ln -sf /usr/lib/x86_64-linux-gnu/libncurses.so.6 compat/libncurses.so.5
    ln -sf /usr/lib/x86_64-linux-gnu/libtinfo.so.6 compat/libtinfo.so.5
    export LD_LIBRARY_PATH="$b/compat${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
fi
xvlog -sv officers_core.sv "$here/tb_officers.sv" > xvlog.out
xelab -O3 tb_officers -generic_top "PRELOAD=$preload" -generic_top "NRUN=$nrun" -s tb > xelab.out
xsim tb -runall | grep -E "RESULT|MISMATCH|Error|Fatal"
