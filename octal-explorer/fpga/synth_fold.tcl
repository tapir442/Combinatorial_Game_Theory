# =============================================================================
# synth_fold.tcl -- area and clock of the whole board design at another fold P
#
#   cd fpga/build/fold-logs         (Vivado drops its .log/.jou in the cwd)
#   vivado -mode batch -source ../../synth_fold.tcl -tclargs P
#
# The clock is the board's: 200 MHz from the MMCM in nexys_a7_top.
#
# Generates the folded core for P (gen_folded.py) into fpga/build/fold-p<P>/,
# then synthesizes, places and routes nexys_a7_top (core + UART + period
# search) with the board's constraints, in non-project mode.  It does not touch
# vivado/ (the board project, whose create_project.tcl -force would wipe the
# bitstream in use) or build/gen/.  No bitstream is written: the point is the
# LUT count and WNS that a P other than 8 would leave for a split check.
#
# Output: fpga/build/fold-p<P>/
#   officers_folded.sv, folded_info.svh, folded_params.json   (generated)
#   impl_util.rpt, impl_timing.rpt                            (after routing)
#
# Vivado needs build/compat on LD_LIBRARY_PATH (libncurses.so.5), see the Makefile.
# =============================================================================
set p   [lindex $argv 0]
set here [file dirname [file normalize [info script]]]
set out  "$here/build/fold-p$p"
file mkdir $out
set_param general.maxThreads 8

# ---- generated core, as in create_project.tcl (helpers without Vivado's env) ----
proc run {args} { puts [exec env -u LD_LIBRARY_PATH -u PYTHONHOME -u PYTHONPATH {*}$args] }
set octal [file normalize "$here/../octal"]
run $octal .6 -n 100000 -p --rare -q > "$out/values-table.txt"
run /usr/bin/python3 "$here/gen_folded.py" --values "$out/values-table.txt" --fold $p -o $out

# ---- sources and constraints ----
read_verilog -sv [list "$out/officers_folded.sv" "$here/ffs256.sv" "$here/uart.sv" \
                       "$here/window_fp.sv" "$here/nexys_a7_top.sv"]
read_xdc "$here/nexys_a7.xdc"

# ---- synthesis and implementation ----
synth_design -top nexys_a7_top -part xc7a100tcsg324-1 -include_dirs $out
opt_design
place_design
route_design
report_utilization    -file "$out/impl_util.rpt"
report_timing_summary -file "$out/impl_timing.rpt"
