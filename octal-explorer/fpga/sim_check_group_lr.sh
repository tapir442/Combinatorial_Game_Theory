#!/bin/bash
# xsim of check_group_lr against the witness depths of gen_check_vectors.py.
#   sim_check_group_lr.sh [GS] [N0] [HEAPS]      (defaults 4 100000 400)
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
gs=${1:-4}; n0=${2:-100000}; heaps=${3:-400}
out=$here/build/sim_check_group_lr
mkdir -p "$out" && cd "$out"
env -u LD_LIBRARY_PATH -u PYTHONHOME -u PYTHONPATH /usr/bin/python3 "$here/gen_check_vectors.py" "$out" "$n0" "$heaps"
export LD_LIBRARY_PATH=$here/build/compat:${LD_LIBRARY_PATH:-}
xvlog -sv "$here/check_group_lr.sv" "$here/tb_check_group_lr.sv" > xvlog.log
xelab -debug off tb_check_group_lr -generic_top "GS=$gs" -generic_top "NMAX=$((n0 + heaps))" -generic_top "HMAX=$heaps" -s tb_check_group_lr > xelab.log
xsim tb_check_group_lr -R -testplusarg "dir=$out" > xsim.log
grep -E "heaps|PASS|FAIL|ERROR|Fatal" xsim.log
