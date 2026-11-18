# =============================================================================
# synth_check.tcl -- resource and clock estimate for the split-check lanes
#
#   cd fpga/build/check-logs        (Vivado drops its .log/.jou in the cwd)
#   vivado -mode batch -source ../../synth_check.tcl -tclargs GS W [MHZ] [TOP] [NC]
#
# TOP is check_group (default), check_group_lr (the LUT-RAM lanes; W must
# be 1 there, it has no W parameter) or check_cluster (NS groups of GS LUT-RAM
# lanes with their block RAMs; the second argument is then NS) or check_top
# (job builder + NC clusters of NS groups; NC defaults to 2) or check_deep (the
# DDR2 fallback lane; GS and W are ignored, give MHZ = 81.25, the MIG user clock).
#
# Synthesizes TOP with GS lanes of W pairs per clock, then places
# and routes it on its own ("out of context": no board, no pins, no top level),
# on the Nexys A7's part.  The point is a LUT/FF count per lane and whether the
# lane pipeline makes 200 MHz -- not a usable netlist.
#
# Output: fpga/build/check-gs<GS>-w<W>/ (check_group_lr: check-lr-gs<GS>/,
# check_cluster: check-cl-gs<GS>-ns<NS>/, check_top: check-top-gs<GS>-ns<NS>-nc<NC>/)
#   clock.xdc         the clock constraint written below
#   synth_util.rpt    utilization after synthesis
#   impl_util.rpt     utilization after place and route (the number to quote)
#   impl_timing.rpt   timing summary after routing (WNS at MHZ)
#
# Caveat: in out-of-context mode the ports are not real pins, so the wide `win`
# and `pre` inputs come for free here.  On the board they are block-RAM outputs;
# their routing to GS lanes is not part of this estimate.
#
# Vivado needs build/compat on LD_LIBRARY_PATH (libncurses.so.5), as for the
# other scripts in this directory (see the Makefile).
# =============================================================================

# ---- arguments: group size, pairs per lane and clock, target clock ----
set gs  [lindex $argv 0]
set w   [lindex $argv 1]
set mhz [expr {[llength $argv] > 2 ? [lindex $argv 2] : 200}]
set top [expr {[llength $argv] > 3 ? [lindex $argv 3] : "check_group"}]
set nc  [expr {[llength $argv] > 4 ? [lindex $argv 4] : 2}]

# The Nexys A7-100T's part (Artix-7 100T, speed grade -1), as in create_project.tcl.
set part xc7a100tcsg324-1

# Paths relative to this script, so the cwd only decides where the logs go.
set here [file dirname [file normalize [info script]]]
switch $top {
    check_group    { set out "$here/build/check-gs$gs-w$w" }
    check_group_lr { set out "$here/build/check-lr-gs$gs" }
    check_cluster  { set out "$here/build/check-cl-gs$gs-ns$w" }
    check_top      { set out "$here/build/check-top-gs$gs-ns$w-nc$nc" }
    check_deep     { set out "$here/build/check-deep" }
    default        { error "unknown TOP $top" }
}
if {[info exists ::env(PHYS_OPT)] && $::env(PHYS_OPT)} { append out "-po" }
file mkdir $out

# Synthesis and placement use at most 8 threads (the board service and other
# jobs share the machine).
set_param general.maxThreads 8

# ---- sources ----
if {$top in {check_cluster check_top}} {
    read_verilog -sv [list "$here/check_pkg.sv" "$here/check_group_lr.sv" "$here/check_stream.sv" \
                           "$here/check_cluster.sv" "$here/check_top.sv"]
} elseif {$top eq "check_deep"} {
    read_verilog -sv [list "$here/check_pkg.sv" "$here/check_deep.sv"]
} else {
    read_verilog -sv "$here/$top.sv"
}

# ---- constraint: one clock on the port clk at MHZ ----
# Written fresh for each run; the period is 1000 / MHZ ns (5.000 ns at 200 MHz).
set xdc "$out/clock.xdc"
set f [open $xdc w]
puts $f "create_clock -name clk -period [expr {1000.0 / $mhz}] \[get_ports clk\]"
close $f
read_xdc -mode out_of_context $xdc

# ---- synthesis ----
# -mode out_of_context: no I/O buffers are inserted on the ports.
# -generic overrides the module parameters GS and W (check_group_lr has no W;
# check_cluster takes NS instead).
set generics [expr {$top eq "check_deep" ? {} : [list -generic GS=$gs]}]
if {$top eq "check_group"}   { lappend generics -generic W=$w }
if {$top in {check_cluster check_top}} { lappend generics -generic NS=$w }
if {$top eq "check_top"}     { lappend generics -generic NC=$nc }
synth_design -top $top -part $part -mode out_of_context {*}$generics
report_utilization -file "$out/synth_util.rpt"

# ---- implementation: optimize, place, route ----
# No phys_opt_design by default: this is an estimate, and the default flow is
# closer to what a first board build would get.  PHYS_OPT=1 in the environment
# adds it after placement (output directory gets a -po suffix).
opt_design
place_design
if {[info exists ::env(PHYS_OPT)] && $::env(PHYS_OPT)} { phys_opt_design }
route_design

# ---- reports ----
report_utilization -file "$out/impl_util.rpt"
report_timing_summary -file "$out/impl_timing.rpt"
