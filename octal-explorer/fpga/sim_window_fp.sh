#!/bin/bash
# xsim of window_fp against check_window_fp.py (about a minute).
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
out=$here/build/sim_window_fp
mkdir -p "$out" && cd "$out"
export LD_LIBRARY_PATH=$here/build/compat:${LD_LIBRARY_PATH:-}
xvlog -sv "$here/window_fp.sv" "$here/tb_window_fp.sv" > xvlog.log
xelab -debug off tb_window_fp -s tb_window_fp > xelab.log
xsim tb_window_fp -R > xsim.log; grep -E "sent|ERROR|Fatal" xsim.log || true
env -u LD_LIBRARY_PATH -u PYTHONHOME -u PYTHONPATH /usr/bin/python3 "$here/check_window_fp.py" window_fp.log
