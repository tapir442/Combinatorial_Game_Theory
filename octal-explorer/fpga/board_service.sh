#!/bin/bash
# Keep the Officers long run on the Nexys A7 going (run by officers-board.service).
#
# Loop:
#   * no board on $PORT (or no permission)    -> retry in $RETRY s (default 300)
#   * board does not answer 'I'               -> program it over JTAG (volatile, never the
#                                                flash), then ask again; retry later on failure
#   * run_board.py --no-check --total 0       -> continues from build/board-state.bin
#     - stopped by the service (SIGINT)       -> it finishes its block, saves, we exit
#     - exit 3 (err: speculation failed)      -> stop for good (the state file is kept)
#     - anything else (board unplugged, ...)  -> retry in $RETRY s
# Output goes to the journal and to build/board-run.log.
set -u
here=$(cd "$(dirname "$0")" && pwd)
PORT=${PORT:-/dev/serial/by-id/usb-Digilent_Digilent_USB_Device_210292BF7051-if01-port0}
RETRY=${RETRY:-300}
VIVADO_SETTINGS=${VIVADO_SETTINGS:-/mnt/researches/2026.1/Vivado/settings64.sh}
log=$here/build/board-run.log
mkdir -p "$here/build"

stopping=0
trap 'stopping=1' INT TERM

say() { echo "$(date '+%F %T') $*" | (trap '' INT TERM; exec tee -a "$log"); }

pause() {
    [ $stopping = 1 ] && exit 0
    say "retry in $RETRY s"
    sleep "$RETRY" & wait $!
    [ $stopping = 1 ] && exit 0
}

program() {
    say "board does not answer: programming it over JTAG"
    (
        # the Xilinx settings scripts use unset variables (PYTHONPATH, ...): no set -u here
        set +u
        source "$VIVADO_SETTINGS" >/dev/null || { echo "ERROR: cannot source $VIVADO_SETTINGS"; exit 1; }
        export LD_LIBRARY_PATH=$here/build/compat:${LD_LIBRARY_PATH:-}
        cd "$here" && vivado -mode batch -nojournal -nolog -source program.tcl
    ) > "$here/build/board-program.log" 2>&1
    local rc=$?
    grep -E "PROGRAMMED|ERROR|No devices" "$here/build/board-program.log" | head -3 |
        while read -r l; do say "  $l"; done
    sleep 2
    return $rc
}

info() { python3 "$here/run_board.py" --port "$PORT" --info 2>&1 | while read -r l; do say "$l"; done
         return "${PIPESTATUS[0]}"; }

while :; do
    [ $stopping = 1 ] && exit 0
    if [ ! -e "$PORT" ]; then say "no FPGA board at $PORT"; pause; continue; fi
    if [ ! -r "$PORT" ] || [ ! -w "$PORT" ]; then
        say "no read/write permission for $PORT (see the udev rule in docs/fpga.md)"; pause; continue
    fi
    if ! info; then
        [ $stopping = 1 ] && exit 0
        if ! program || ! info; then pause; continue; fi
    fi
    [ $stopping = 1 ] && exit 0
    say "starting run_board.py"
    # tee ignores SIGINT, so run_board.py can still print and save after the service stops it
    python3 -u "$here/run_board.py" --port "$PORT" --no-check --total 0 2>&1 |
        (trap '' INT TERM; exec tee -a "$log")
    rc=${PIPESTATUS[0]}
    [ $stopping = 1 ] && { say "stopped (run_board.py exit $rc)"; exit 0; }
    if [ "$rc" = 3 ]; then say "speculation failed (err): not restarting"; exit 3; fi
    say "run_board.py exited with $rc"
    pause
done
