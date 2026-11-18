#!/bin/bash
# xsim of check_cluster against the witness depths of gen_check_vectors.py.
#   sim_check_cluster.sh [GS] [NS] [LIM] [N0] [HEAPS]   (defaults 16 8 4096 1000000 2048)
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
gs=${1:-16}; ns=${2:-8}; lim=${3:-4096}; n0=${4:-1000000}; heaps=${5:-2048}
out=$here/build/sim_check_cluster
mkdir -p "$out" && cd "$out"
if [ ! -f "vec-$n0-$heaps/expect.txt" ]; then
    env -u LD_LIBRARY_PATH -u PYTHONHOME -u PYTHONPATH /usr/bin/python3 "$here/gen_check_vectors.py" "vec-$n0-$heaps" "$n0" "$heaps"
fi
export LD_LIBRARY_PATH=$here/build/compat:${LD_LIBRARY_PATH:-}
xvlog -sv "$here/check_group_lr.sv" "$here/check_stream.sv" "$here/check_cluster.sv" "$here/tb_check_cluster.sv" > xvlog.log
xelab -debug off tb_check_cluster -generic_top "GS=$gs" -generic_top "NS=$ns" -generic_top "LIM=$lim" \
      -generic_top "NMAX=$((n0 + heaps))" -generic_top "HMAX=$heaps" -s tb_check_cluster > xelab.log
xsim tb_check_cluster -R -testplusarg "dir=$out/vec-$n0-$heaps" > xsim.log
grep -E "heaps|PASS|FAIL|ERROR|Fatal" xsim.log | head -20
