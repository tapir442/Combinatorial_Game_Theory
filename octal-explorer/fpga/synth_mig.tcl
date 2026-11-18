# =============================================================================
# synth_mig.tcl -- area of the DDR2 controller (MIG 7-series) for the fallback
#
#   cd fpga/build/mig-logs && vivado -mode batch -source ../../synth_mig.tcl
#
# ddr2_mig.prj is Digilent's MIG project for the Nexys A7-100T (board files
# 1.3, nexys-a7-100t/mig.prj): DDR2 MT47H64M16HR at 325 MHz, 4:1 PHY (user
# clock 81.25 MHz), 16 bits, 128 MiB, 100 MHz single-ended system clock, a
# 200 MHz reference clock without buffer.  Changed here: the module name, and
# the native user interface instead of AXI (one streaming user, no AXI shim).
#
# Generates the IP in an in-memory project under fpga/build/mig/ and
# synthesizes it out of context; the report is fpga/build/mig/mig_util.rpt.
# It does not touch vivado/.
# =============================================================================
set here [file dirname [file normalize [info script]]]
set out  "$here/build/mig"
file delete -force $out
file mkdir $out
set_param general.maxThreads 8
create_project -in_memory -part xc7a100tcsg324-1
create_ip -name mig_7series -vendor xilinx.com -library ip -module_name ddr2_mig -dir $out
file copy -force "$here/ddr2_mig.prj" "$out/ddr2_mig/ddr2_mig.prj"
set_property CONFIG.XML_INPUT_FILE ddr2_mig.prj [get_ips ddr2_mig]
generate_target all [get_ips ddr2_mig]
synth_ip [get_ips ddr2_mig]
open_checkpoint "$out/ddr2_mig/ddr2_mig.dcp"
report_utilization -file "$out/mig_util.rpt"
