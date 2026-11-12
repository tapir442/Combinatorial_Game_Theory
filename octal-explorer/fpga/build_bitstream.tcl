# Synthesis, implementation and bitstream in the project (run create_project.tcl first).
#   cd fpga && vivado -mode batch -source build_bitstream.tcl [-tclargs keep]   (or: make bit)
#
# Always rebuilds from synthesis.  Fails (exit 1) instead of reusing old results when
#   - a run left behind by a killed Vivado cannot be reset (stale run directories are
#     removed if no process of theirs is alive; a live one is an error),
#   - synthesis or implementation does not complete,
#   - the bitstream is not newer than the start of this build,
#   - timing is not met (WNS < 0); with "keep" the bitstream is kept but still reported.
set here  [file dirname [file normalize [info script]]]
set runs  "$here/vivado/officers.runs"
set keep  [expr {[lsearch $argv keep] >= 0}]
set start [clock seconds]

proc fail {msg} { puts "BUILD FAILED: $msg"; catch close_project; exit 1 }

# pids recorded by Vivado in a run directory (.<step>.begin.rst) that are still alive
proc live_pids {dir} {
    set pids {}
    foreach f [glob -nocomplain -directory $dir -types hidden *.begin.rst] {
        set fh [open $f]; set txt [read $fh]; close $fh
        foreach {- pid} [regexp -all -inline {Pid="(\d+)"} $txt] {
            if {[file exists "/proc/$pid"]} { lappend pids $pid }
        }
    }
    return [lsort -unique $pids]
}

# clear what a killed run left behind, so that reset_run cannot silently fail
foreach r {synth_1 impl_1} {
    set dir "$runs/$r"
    if {![file isdirectory $dir]} continue
    set pids [live_pids $dir]
    if {[llength $pids]} { fail "$r is still running (pid $pids); stop it first" }
    file delete -force $dir
}
file delete -force "$runs/.jobs"

open_project "$here/vivado/officers.xpr"
foreach r {impl_1 synth_1} {
    if {[catch {reset_run $r} err]} { fail "reset_run $r: $err" }
}

launch_runs impl_1 -to_step write_bitstream -jobs 8
wait_on_run synth_1
set s [get_runs synth_1]
if {[get_property PROGRESS $s] ne "100%" || ![string match "*Complete*" [get_property STATUS $s]]} {
    fail "synthesis: [get_property STATUS $s]"
}
wait_on_run impl_1
set i [get_runs impl_1]
if {![string match "*write_bitstream Complete*" [get_property STATUS $i]]} {
    fail "implementation: [get_property STATUS $i]"
}

set bit [glob -nocomplain "$runs/impl_1/*.bit"]
if {[llength $bit] != 1 || [file mtime [lindex $bit 0]] < $start} {
    fail "no fresh bitstream in $runs/impl_1"
}
set bit [lindex $bit 0]

open_run impl_1
report_utilization -file "$here/vivado/utilization.rpt"
report_timing_summary -max_paths 5 -file "$here/vivado/timing.rpt"
set wns [get_property SLACK [get_timing_paths -max_paths 1 -setup]]
set whs [get_property SLACK [get_timing_paths -max_paths 1 -hold]]
puts "BITSTREAM $bit WNS $wns WHS $whs"
close_project
if {$wns < 0 || $whs < 0} {
    if {$keep} { puts "WARNING: timing not met, bitstream kept (keep)" } else {
        file delete $bit
        puts "BUILD FAILED: timing not met (WNS $wns, WHS $whs), bitstream deleted; see vivado/timing.rpt"
        exit 1
    }
}
