#!/bin/bash
# xsim of check_top (job builder + clusters) fed like the core, against gen_check_vectors.py.
#   sim_check_top.sh [NS] [NC] [LIM] [PCORE] [N0] [HEAPS]   (defaults 4 2 4096 16 1000000 2048)
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
ns=${1:-4}; nc=${2:-2}; lim=${3:-4096}; pc=${4:-16}; n0=${5:-1000000}; heaps=${6:-2048}
out=$here/build/sim_check_top
mkdir -p "$out" && cd "$out"
vec=$here/build/sim_check_cluster/vec-$n0-$heaps
if [ ! -f "$vec/expect.txt" ]; then
    env -u LD_LIBRARY_PATH -u PYTHONHOME -u PYTHONPATH /usr/bin/python3 "$here/gen_check_vectors.py" "$vec" "$n0" "$heaps"
fi
export LD_LIBRARY_PATH=$here/build/compat:${LD_LIBRARY_PATH:-}
xvlog -sv "$here/check_pkg.sv" "$here/check_group_lr.sv" "$here/check_stream.sv" "$here/check_cluster.sv" "$here/check_top.sv" \
      "$here/tb_check_top.sv" > xvlog.log
xelab -debug off tb_check_top -generic_top "NS=$ns" -generic_top "NC=$nc" -generic_top "LIM=$lim" \
      -generic_top "PCORE=$pc" -generic_top "NMAX=$((n0 + heaps))" -generic_top "HMAX=$heaps" \
      -s tb_check_top > xelab.log
xsim tb_check_top -R -testplusarg "dir=$vec" > xsim.log
grep -E "heaps|PASS|FAIL|ERROR|Fatal" xsim.log | head -20
