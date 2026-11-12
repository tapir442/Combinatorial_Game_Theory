# Out-of-context synthesis + implementation of officers_core.
#   vivado -mode batch -source fpga/synth.tcl -tclargs PART [MHZ] [impl]
# Writes reports to fpga/build/<part>/.  No board or I/O planning: the core is
# placed and routed on its own to get resource use and the achievable clock.
set part [lindex $argv 0]
set mhz  [expr {[llength $argv] > 1 ? [lindex $argv 1] : 400}]
set impl [expr {[llength $argv] > 2 ? [lindex $argv 2] : "impl"}]
set here [file dirname [file normalize [info script]]]
set out  "$here/build/$part"
file mkdir $out
set_param general.maxThreads 16

read_verilog -sv "$here/build/officers_core.sv"
set xdc "$out/clock.xdc"
set f [open $xdc w]
puts $f "create_clock -name clk -period [expr {1000.0 / $mhz}] \[get_ports clk\]"
close $f
read_xdc -mode out_of_context $xdc
synth_design -top officers_core -part $part -mode out_of_context -flatten_hierarchy rebuilt
report_utilization -file "$out/synth_util.rpt"
report_timing_summary -file "$out/synth_timing.rpt"
write_checkpoint -force "$out/synth.dcp"
if {$impl eq "impl"} {
    opt_design
    place_design
    phys_opt_design
    route_design
    report_utilization -file "$out/impl_util.rpt"
    report_timing_summary -max_paths 10 -file "$out/impl_timing.rpt"
    write_checkpoint -force "$out/impl.dcp"
}
