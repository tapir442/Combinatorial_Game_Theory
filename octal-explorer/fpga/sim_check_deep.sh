#!/bin/bash
# xsim of check_deep (fallback lane on DDR2) with a MIG model, against gen_check_vectors.py.
#   sim_check_deep.sh [PRE] [NLO] [RDY_PCT] [N0] [HEAPS]   (defaults 65536 900000 70 1000000 4096)
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
pre=${1:-65536}; nlo=${2:-900000}; rdy=${3:-70}; n0=${4:-1000000}; heaps=${5:-4096}
out=$here/build/sim_check_deep
mkdir -p "$out" && cd "$out"
vec=$here/build/sim_check_cluster/vec-$n0-$heaps
if [ ! -f "$vec/expect.txt" ]; then
    env -u LD_LIBRARY_PATH -u PYTHONHOME -u PYTHONPATH /usr/bin/python3 "$here/gen_check_vectors.py" "$vec" "$n0" "$heaps"
fi
export LD_LIBRARY_PATH=$here/build/compat:${LD_LIBRARY_PATH:-}
xvlog -sv "$here/check_pkg.sv" "$here/check_deep.sv" "$here/tb_check_deep.sv" > xvlog.log
xelab -debug off tb_check_deep -generic_top "PRE=$pre" -generic_top "NLO=$nlo" -generic_top "RDY_PCT=$rdy" \
      -generic_top "NMAX=$((n0 + heaps))" -generic_top "HMAX=$heaps" -s tb_check_deep > xelab.log
xsim tb_check_deep -R -testplusarg "dir=$vec" > xsim.log
grep -E "deep:|PASS|FAIL|ERROR|Fatal" xsim.log | head -20
