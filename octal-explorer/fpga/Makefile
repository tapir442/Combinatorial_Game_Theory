# Officers (.6) speculation core on an FPGA (Grossman 2017, section 4).  See ../docs/fpga.md.
#   make model          quick check of the pipeline in Python (seconds)
#   make sim            RTL simulation with xsim against octal (about 15 min)
#   make sim-folded     the same for the folded (board) core
#   make synth          out-of-context synthesis (about 30 min, 21 GB RAM)
#   make impl           synthesis + place and route (adds about an hour)
#   make project        Vivado project vivado/officers.xpr (Nexys A7-100T, folded core)
#   make bit / program  bitstream in the project / load it over JTAG
#   make board          run on the board and compare with octal (run_board.py)
#   make run N=1e10     long run on the board without checking (display: highest value);
#                       continues from build/board-state.bin (FRESH=--fresh starts at N0)
#   make service-install / service-uninstall   systemd user service officers-board
# Vivado must be on PATH (source <install>/Vivado/settings64.sh).
PART  ?= xc7vx690tffg1761-2
MHZ   ?= 232
N0    ?= 50000
NRUN  ?= 3000
FOLD  ?= 8
OCTAL ?= ../octal

# Vivado 2026.1 / xsim still link libncurses.so.5 and libtinfo.so.5; the .so.6
# ABI works, so a private directory of symlinks is put on LD_LIBRARY_PATH.
COMPAT := $(CURDIR)/build/compat
export LD_LIBRARY_PATH := $(COMPAT)$(if $(LD_LIBRARY_PATH),:$(LD_LIBRARY_PATH))

TOTAL := $(shell echo $$(( $(N0) + $(NRUN) )))
VALUES := build/values-$(TOTAL).txt

$(VALUES): $(OCTAL)
	mkdir -p build
	$(OCTAL) .6 -n $(TOTAL) -p --rare -q > $@

# the rare table needs only the first ~20628 values, so the core does not
# depend on N0 / NRUN; it is made from a fixed 100000 values
build/officers_core.sv: gen_officers.py $(OCTAL)
	mkdir -p build
	$(OCTAL) .6 -n 100000 -p --rare -q > build/values-table.txt
	python3 gen_officers.py --values build/values-table.txt -o build

$(COMPAT)/libncurses.so.5:
	mkdir -p $(COMPAT)
	ln -sf /usr/lib/x86_64-linux-gnu/libncurses.so.6 $(COMPAT)/libncurses.so.5
	ln -sf /usr/lib/x86_64-linux-gnu/libtinfo.so.6 $(COMPAT)/libtinfo.so.5

model: build/officers_core.sv $(VALUES)
	python3 model.py build/officers_params.json $(VALUES) $(N0) $(NRUN)

sim: $(COMPAT)/libncurses.so.5
	./sim.sh $(N0) $(NRUN)

# folded core: 300 values with 20 % random stalls (STALL= percent)
STALL ?= 20
sim-folded: $(COMPAT)/libncurses.so.5
	./sim_folded.sh $(N0) 300 $(FOLD) $(STALL)

synth impl: build/officers_core.sv $(COMPAT)/libncurses.so.5
	mkdir -p build/$(PART)
	cd build/$(PART) && vivado -mode batch -nojournal -log $@.log \
	    -source ../../synth.tcl -tclargs $(PART) $(MHZ) $(if $(filter impl,$@),impl,synth)

clean:
	rm -rf build

# ---- Vivado project for the Nexys A7-100T (folded core + UART) ----
project: $(COMPAT)/libncurses.so.5
	vivado -mode batch -nojournal -nolog -source create_project.tcl -tclargs $(FOLD) $(N0) 300

bit: $(COMPAT)/libncurses.so.5
	vivado -mode batch -nojournal -log vivado/build.log -source build_bitstream.tcl

program: $(COMPAT)/libncurses.so.5
	vivado -mode batch -nojournal -nolog -source program.tcl

board:
	python3 run_board.py --n0 $(N0)

# long run on the board without checking (N values, 0 = until Ctrl-C)
N ?= 1e10
run:
	python3 run_board.py --n0 $(N0) --no-check --total $(N) $(FRESH)

service-install:
	mkdir -p $(HOME)/.config/systemd/user
	ln -sf $(CURDIR)/officers-board.service $(HOME)/.config/systemd/user/officers-board.service
	systemctl --user daemon-reload
	systemctl --user enable officers-board
	@echo "enabled; start it with: systemctl --user start officers-board"

service-uninstall:
	-systemctl --user disable --now officers-board
	rm -f $(HOME)/.config/systemd/user/officers-board.service
	systemctl --user daemon-reload

.PHONY: model sim sim-folded synth impl clean project bit program board run service-install service-uninstall
