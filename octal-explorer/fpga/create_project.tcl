# Create the Vivado project fpga/vivado/officers.xpr (Nexys A7-100T).
#
#   cd fpga && vivado -mode batch -source create_project.tcl [-tclargs FOLD N0 NRUN]
#   (or: make project)  then  vivado vivado/officers.xpr
#
# The project is recreated from this script, so fpga/vivado/ is not under
# version control.  The cores are generated first into fpga/build/gen/ by
# gen_folded.py (board design) and gen_officers.py (full one-value-per-clock
# core, simulation set sim_full only: it needs a Virtex-7 690T-class part).
#
# Filesets:
#   sources_1  nexys_a7_top (UART + folded core), constrs_1 nexys_a7.xdc
#   sim_1      tb_folded   -- folded core against octal (active)
#   sim_full   tb_officers -- full core against octal
set fold [expr {[llength $argv] > 0 ? [lindex $argv 0] : 8}]
set n0   [expr {[llength $argv] > 1 ? [lindex $argv 1] : 50000}]
set nrun [expr {[llength $argv] > 2 ? [lindex $argv 2] : 300}]

set here  [file dirname [file normalize [info script]]]
set gen   "$here/build/gen"
set octal [file normalize "$here/../octal"]
set python /usr/bin/python3   ;# not the python3 that Vivado puts on PATH
file mkdir $gen

# ---- generated sources and test vectors ----
# helpers run without Vivado's LD_LIBRARY_PATH and Python settings (its old libstdc++ breaks octal's ROCm libraries)
proc run {args} { puts [exec env -u LD_LIBRARY_PATH -u PYTHONHOME -u PYTHONPATH {*}$args] }
run $octal .6 -n 100000 -p --rare -q > "$gen/values-table.txt"
run $octal .6 -n [expr {$n0 + $nrun}] -p --rare -q > "$gen/values.txt"
run $python "$here/gen_folded.py" --values "$gen/values-table.txt" --fold $fold -o $gen
run $python "$here/gen_officers.py" --values "$gen/values-table.txt" -o $gen
run $python "$here/make_vectors.py" $gen $n0 $nrun
set fj [open "$gen/folded_params.json"]; set fp [read $fj]; close $fj
regexp {"preload": (\d+)} $fp -> pre_folded
set fj [open "$gen/officers_params.json"]; set fp [read $fj]; close $fj
regexp {"preload": (\d+)} $fp -> pre_full

# ---- project ----
create_project officers "$here/vivado" -part xc7a100tcsg324-1 -force
set_property target_language Verilog [current_project]
set_property simulator_language Verilog [current_project]

add_files -fileset sources_1 [list \
    "$gen/officers_folded.sv" "$gen/folded_info.svh" \
    "$here/ffs256.sv" "$here/uart.sv" "$here/window_fp.sv" "$here/nexys_a7_top.sv"]
set_property file_type SystemVerilog [get_files -of_objects [get_filesets sources_1] *.sv]
set_property file_type {Verilog Header} [get_files folded_info.svh]
set_property include_dirs [list $gen] [get_filesets sources_1]
set_property top nexys_a7_top [get_filesets sources_1]

add_files -fileset constrs_1 "$here/nexys_a7.xdc"

# sim_1: folded core
add_files -fileset sim_1 [list "$here/tb_folded.sv" \
    "$gen/sim_folded/preload.hex" "$gen/sim_folded/expected.hex"]
set_property file_type SystemVerilog [get_files "$here/tb_folded.sv"]
set_property file_type {Memory Initialization Files} \
    [get_files -of_objects [get_filesets sim_1] *.hex]
set_property top tb_folded [get_filesets sim_1]
set_property generic "PRELOAD=$pre_folded NRUN=$nrun" [get_filesets sim_1]
set_property -name {xsim.simulate.runtime} -value {all} -objects [get_filesets sim_1]

# sim_full: the one-value-per-clock core (Grossman section 4); about 15 min in xsim
create_fileset -simset sim_full
add_files -fileset sim_full [list "$gen/officers_core.sv" "$here/tb_officers.sv" \
    "$gen/sim_full/preload.hex" "$gen/sim_full/expected.hex"]
set_property SOURCE_SET {} [get_filesets sim_full]
set_property file_type SystemVerilog [get_files -of_objects [get_filesets sim_full] *.sv]
set_property file_type {Memory Initialization Files} \
    [get_files -of_objects [get_filesets sim_full] *.hex]
set_property top tb_officers [get_filesets sim_full]
set_property generic "PRELOAD=$pre_full NRUN=$nrun" [get_filesets sim_full]
set_property -name {xsim.simulate.runtime} -value {all} -objects [get_filesets sim_full]
current_fileset -simset [get_filesets sim_1]

update_compile_order -fileset sources_1
puts "created $here/vivado/officers.xpr (fold $fold; tests from n0 = $n0, $nrun values)"
close_project
