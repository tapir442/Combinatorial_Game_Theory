# Load the bitstream into the FPGA over JTAG (volatile: gone after power-off).
#   cd fpga && vivado -mode batch -source program.tcl     (or: make program)
set here [file dirname [file normalize [info script]]]
set bit [lindex [glob "$here/vivado/officers.runs/impl_1/*.bit"] 0]
open_hw_manager
connect_hw_server
open_hw_target
set dev [lindex [get_hw_devices xc7a100t*] 0]
current_hw_device $dev
set_property PROGRAM.FILE $bit $dev
program_hw_devices $dev
puts "PROGRAMMED $dev with $bit"
close_hw_target
disconnect_hw_server
