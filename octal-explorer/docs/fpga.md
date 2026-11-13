# Officers on an FPGA

Grossman ends *Searching for periodicity in Officers* (GONC 5, 2017, §4) with
this idea:

> implement the unrolled single-threaded algorithm directly in hardware using a
> field programmable gate array (FPGA). The algorithm is extremely well suited
> to such an implementation, and would easily be able to generate a new value
> on every clock cycle.

`fpga/` contains that core for Officers (`.6`). It is the **speculation** of
§3.1 (Figure 3), the same step 1 that `octal --rare` runs on the speculation
threads (see [rare-value algorithm](rare-values.md)). It assumes there are no
new rare values, and streams out one speculative G(n) per clock. As on the CPU,
those values still have to be **checked** (step 2) before they count. The FPGA
does not do that part.

There are two versions:

| | full core | folded core |
|---|---|---|
| generator | `gen_officers.py` | `gen_folded.py` |
| values per clock | 1 | 1 / P (P = 8) |
| size | 265k LUTs | 41k LUTs, 46k flip-flops |
| part | Virtex-7 690T (out of context only) | **Nexys A7-100T (Artix-7 100T), runs on the board** |
| clock | 232.6 MHz, timing met | **200 MHz on the board: 25 M values/s** |

Status:
* **Full core:** simulated against `octal`, and placed and routed for a
  Virtex-7 at 232 MHz. It has not run on a board, since no board that large is
  available.
* **Folded core:** runs on a Nexys A7-100T over the USB-UART at 200 MHz, which
  gives 25 M values/s. That is about 6.5 times `octal --rare` on 24 threads, and
  5 times Grossman's 5 M values/s. 1.01 · 10⁷ values match `octal`, checked
  value by value and by CRC.

## The full core

For Officers, G(n) = mex({G(n−1)} ∪ {G(a) ⊕ G(n−1−a) : 1 ≤ a ≤ n−2}). A value is
rare iff `popcount(v & 0x1EE)` is even. The 1584 rare heap sizes (0, 1, 2, 4, …,
20627) give step 1:

    M(n) = { G(a) ⊕ G(n−1−a) : a rare }       (a = 0 is the one-heap move)
    G(n) ≈ smallest common value not in M(n)

Only common marks matter (rare ⊕ common = common). Below 512 there are exactly
256 common values, and each one is determined by its low 8 bits. So the mex
array is a 256-bit register, and position p stands for the p-th smallest
common value.

```
          load_data ─┐
                     ▼
   ┌──► H[0] ─ H[1] ─ … ─ H[20624]     history H[j] = G(n−1−j), 9 bits
   │      │      │ 1583 fixed taps │
   │      │      ▼                 ▼
   │      │   predecode: 16 + 16 nibble lines per tap
   │      │      ▼
   │      │   L1: 88 groups × 256 bits, bit p = OR over 18 taps of hi[x] & lo[y]
   │      │      ▼
   │      │   L2: 9 × 256  ──►  M0 (256 bits, all rare a ≥ 1, for heap n)
   │      ▼                       │
   │   onehot(G(n−1)) ──── OR ────┘
   │                        ▼
   └──────── G(n) = first free common value  ──►  out_data
```

* **History.** This is a 20625-entry shift register of {common flag, low 8
  bits}. Vivado maps the stretches between taps to SRLs (9.2k LUTs). A
  preloaded rare value has its flag cleared, so it never marks anything.
* **Terms.** Each rare heap a ≥ 1 is one tap. Its value is predecoded once into
  16 + 16 lines (high and low nibble). Mask bit p of that term is then a single
  AND of two lines, with constant indices from low8(cv[p] ⊕ G(a)). That way one
  LUT6 covers three (term, bit) pairs.
* **Pipelined OR tree.** The tree has T = 3 register stages. For heap n+1 it
  reads tap H[a − T]. The rare heaps a < T (1 and 2) are ORed into the last
  stage from H[a − 1]. Every heap a ≥ 1 is at least one cycle old, so all of
  this sits outside the loop.
* **The loop.** It is only G(n) = ffs(¬(M0 | onehot(G(n−1)))), with a radix-4
  find-first-set over the 256 positions in value order.
* **Clock enable.** One enable (`load | run`) drives every register. Stalls and
  gaps in the preload therefore keep the pipeline consistent with the history.
* **Interface.** Preload `PRELOAD` = 20629 values G(n0 − 20629 … n0 − 1) with
  `load`, then hold `run`. `out_data` gives G(n0), G(n0+1), … one cycle after
  each `run`. `err` latches if all 256 common values were marked. Then n0 must be
  at least 20629.

The rare table and the mask positions are constants generated into the RTL by
`fpga/gen_officers.py`. A new rare value makes the speculation wrong anyway,
since the check rejects it, and it means generating a new core.

## Verification

| check | what | result |
|---|---|---|
| `make model` | Python model with the same taps, stages and clock enable; random preload gaps, 20 % stalls | n0 = 20629, 50000, 150000: 0 mismatches against `octal --rare` |
| `make sim` | xsim simulation of the generated RTL (20629-cycle preload, 3000 values, random gaps and 20 % stalls) | 3000 values from n0 = 50000, 0 mismatches, `err` = 0 |
| `make sim-folded` | the folded core in xsim (fast preload with gaps, 2 slow loads, 300 values, 20 % stalls) | v1: 300 values from n0 = 50000, 0 mismatches; v2: the same, with 574 stalled clocks (51 min in xsim, since the data path now switches every clock) |
| `make board` | the folded core on the Nexys A7, see below | v1 at 80 MHz and v2 at 200 MHz: 1.01 · 10⁷ values each, 0 mismatches |

xsim manages only about 40 cycles/s on this design, so most of the 13 minutes
go to the preload. The Python model is the quick check of the generator.

## Results (XC7VX690T-2, out of context)

Vivado 2026.1, BASIC license. That license covers 7-series parts, but not
Kintex/Virtex UltraScale+.

| | |
|---|---|
| LUTs | 265k of 433k (61 %), of which about 9k SRL |
| flip-flops | 41k (5 %) |
| BRAM / DSP | 0 / 0 |
| clock | **232.6 MHz (4.3 ns), timing met** after routing (WNS +0.013 ns, 0 failing endpoints) |
| critical path | the loop H[0] → onehot → OR → ffs → H[0], 8 LUT levels, 84 % routing |
| at 300 MHz | misses by 0.88 ns on the loop; the OR tree alone would reach about 260 MHz |
| synthesis / implementation | 27 min, 21 GB peak / about 15 min, 5 GB |

At 232 MHz the core produces **2.3 · 10⁸ values/s**. For comparison,
`octal --rare` on 24 threads runs at about 3.8 · 10⁶ values/s (.6 to 10⁷ in
2.6 s), and Grossman's baseline rare-value algorithm ran at 8.8 · 10⁴ values/s.
Grossman estimated 500 MHz. On a 7-series part with a 256-bit find-first-set in
the loop, about half of that is realistic.

## The folded core

The full core does not fit anything smaller than a Virtex-7 690T. The folded
core trades speed for area. It evaluates the 1583 taps a ≥ 1 over P = 8 clocks
in S = ⌈1583 / 8⌉ = 198 *slots*. Each clock, a slot reads one of its 8 history
taps, chosen by the window phase. It XORs that tap's rare value G(a) into the
low byte before the decode. Because of that XOR, every slot has the same
256-bit decode and no per-tap constants, so the whole OR tree is shared by all
8 phases. The result is one value every 8 clocks from 41k LUTs, which is 65 % of
an Artix-7 100T.

Timing, with H[j] = G(m−1−j) after the shift that ends period m−1:

* The window W_m runs from phase 2 of period m to phase 1 of period m+1. During
  it H[j] = G(m−j), and window phase w feeds tap set w of every slot. Heap m+2
  needs G(m+1−a) = H[a−1] for each rare a ≥ 1.
* The phase results are ORed into an accumulator, and the last phase writes
  RES. The a = 0 term onehot(G(n−1)) is added afterwards, and the first free
  common value is shifted into H at phase 1.

Two versions of this pipeline exist:

| | v1 (first board version, 80 MHz) | v2 (`gen_folded.py` now, **200 MHz on the board**) |
|---|---|---|
| data stages D | 4: slot mux, predecode, 18-slot group OR (2 LUT levels), group merge | 5: slot mux, predecode, 3-slot AND-OR, 6-way OR, 6-way OR (1 LUT level each) |
| a = 0 term | separate stage at phase 0: Mf = RES ∨ onehot(H[0]) | merged into the final RES write |
| first free value | 1 clock (radix-4 tree + lookup, 7 LUT levels) | 3 clocks: 16-bit chunks, across chunks, lookup |
| constraint | P ≥ D + 2 | P ≥ D + F = 8 |
| stalls | one global clock enable on every register | data path runs every clock with a *valid* tag; only the history shift has an enable, registered one clock ahead |
| interface | `ce`, `ld_fast`, `ld_slow`, `at_shift` | `ce_d`, `ld_fast_d` (for the next clock), `ld_slow`, `shifting` |

The valid tag works like this. A window phase counts only in the clock in which
the phase counter moves on. While the core is stalled, the pipeline keeps
re-issuing the same phase from the unchanged history. The accumulator ignores
those repeats, so a stall costs nothing in consistency.

**Preload.** First 20,627 values are shifted in fast, one per clock, with the
engine frozen. Then the last 2 go through full periods (`ld_slow`), so that RES
for the first computed heap has been built from a complete window. So
n0 ≥ 20,629.

## On the board (Nexys A7-100T)

`nexys_a7_top.sv` wraps the core with an MMCM, a 2 Mbaud UART on the board's
USB port, and a command FSM. All numbers are little-endian:

| command | reply |
|---|---|
| `I` | `I` fold:u8 mhz:u16 depth:u32 slots:u16 |
| `L` n:u32, n × u16 | load G(n0−n … n0−1); the last two go through full periods → `l` |
| `S` n:u32 | compute n values and stream each as u16, then `s` err:u8 |
| `C` n:u32 | compute n values at full speed → `c` crc:u32 cycles:u64 last:u16 err:u8 max:u16 at:u32 (CRC-32 as zlib, over the values as u16; max = highest value of the block, first at value number at, from 0) |

`S` and `C` continue from where the previous command stopped. The LEDs show:

* 0: busy
* 1: error
* 2: MMCM locked
* 15–8: progress

The 7-segment display shows the **highest nim-value so far**, preloaded or
computed, in decimal. Each `L` clears it.

`run_board.py` drives all of this and checks each step against `octal --rare`.
With v2 at 200 MHz:

```
board: fold 8, 200 MHz, history 20627, 198 slots -> 25.00 M values/s
octal --rare: 10150000 values in 4.8 s
loaded 20629 values in 0.22 s
stream: 100000 values from n = 50000 in 1.02 s, 0 mismatches, err = 0
crc run: 10000000 values from n = 150000: crc 7186fe56 (octal 7186fe56), last 154 (octal 154), err = 0 -> OK
  80000001 cycles = 400.0 ms at 200 MHz -> 25.00 M values/s (host round trip 0.57 s)
```

v1 at 80 MHz gave the same values and CRC:

```
board: fold 8, 80 MHz, history 20627, 198 slots -> 10.00 M values/s
octal --rare: 10150000 values in 5.5 s
loaded 20629 values in 0.22 s
stream: 100000 values from n = 50000 in 1.02 s, 0 mismatches, err = 0
crc run: 10000000 values from n = 150000: crc 7186fe56 (octal 7186fe56), last 154 (octal 154), err = 0 -> OK
  80000000 cycles = 1000.0 ms at 80 MHz -> 10.00 M values/s (host round trip 1.17 s)
```

The display then read **319**. That is the largest G(n) for n = 29,371 …
10,149,999, and it first occurs at n = 1,274,955.

**Long runs.** `make -C fpga run N=1e10` (or `run_board.py --no-check --total N
--block 1e9`) runs N values without octal; `N=0` runs until Ctrl-C. Only the
preload comes from octal. The host sends `C` commands of 10⁹ values each. It
prints the CRC and the last value of each block, and the display shows the
highest nim-value so far. Here is a test with 3 · 10⁸ values:

```
n = 100,050,000: block crc 59249092, G(100049999) = 145, err = 0, 25.00 M values/s, 100,000,000 values in 4 s
n = 200,050,000: block crc bcf408c4, G(200049999) = 164, err = 0, 25.00 M values/s, 200,000,000 values in 8 s
n = 300,050,000: block crc 8011f5a3, G(300049999) = 217, err = 0, 25.00 M values/s, 300,000,000 values in 12 s
```

`octal --rare` gives the same G(100,049,999) = 145. These values are the
speculation, so they assume no new rare values. Grossman's computation confirms
that assumption up to 1.4 · 10¹⁴. The board could not detect a new rare value
itself, because that is the check (step 2). `err` only flags a heap where all 256
common values are marked.

The board stores nothing and computes only from the preload: the history it
needs is the last 20,627 values, and those are sent by the host. Programming
uses JTAG only (`make program`), so the design is gone after a power cycle.

### Clock

At 150 MHz, v1 failed by about 4 ns. After placement WNS was −5.0 ns, and the
router was still at −4.3 ns after 1¾ hours. The worst path into each stage of
the *routed* 80 MHz design (v1) was:

| path | delay | of which routing | LUT levels |
|---|---|---|---|
| FSM → global `ce` → clock enables of the whole core | 10.2 ns | 8.9 ns | 7 |
| phase register → slot mux select (198 × 9 muxes) | 10.3 ns | 9.4 ns | 2 |
| first free value → history H[0] | 8.6 ns | 7.2 ns | 7 |
| predecode → group OR | 7.5 ns | 6.6 ns | 2 |
| group OR → merge | 7.3 ns | 6.6 ns | 2 |

Everything is dominated by routing. The first three rows were the targets for
v2: no combinational enable, a replicated registered phase select, and a
pipelined first-free search.

With those changes the core met 200 MHz on its first build. The remaining
failures were all in the board's command logic. Fixing them took three more
builds, each about 30 minutes:

* the UART FIFO's fill count fed the next-clock enable through a subtractor;
* 32-bit compares on the remaining count and on `done == n`;
* a 64-bit cycle counter;
* the display's maximum compare.

Each of these is now a registered flag, a split counter, or a register stage.
The FIFO also shrank from 1024 to 256 bytes, which shortens its read mux.

Final v2 build at 200 MHz:

| | |
|---|---|
| WNS / WHS | +0.231 ns / +0.017 ns |
| worst path | the replicated history-shift enable to the history's clock enables (0 LUT levels) |
| LUTs / flip-flops | 40.9k (64 %) / 46.0k (36 %) |
| build | about 30 min |

The next limit is the history shift enable itself. It reaches about 20k
SRL/flip-flop clock enables across the chip. Going much beyond about 210 MHz
would need P > 8 or a clock-gated (BUFGCE) history.

Digilent's "internal clock speeds exceeding 450 MHz" is the clock *network*
limit of the Artix-7 (BUFG, speed grade −1). It is not a speed that logic with
LUTs and long wires between registers can reach. A core that spans 60 % of the
chip can expect somewhere around 200–250 MHz at best.

## What is missing

* **The check.** Grossman already warned that verifying 5 · 10⁸ values/s would
  need about 6000 cores. Here the CPU check of `octal --rare` handles a few
  10⁶ values/s. That is about 50 times too slow for the full core, and about
  6 times too slow for the folded core on the Nexys A7 at 200 MHz. Either the check moves onto the
  FPGA as well (step 2 needs random access to all of G, i.e. DDR), or the FPGA
  only *searches*: it runs ahead, finds candidates (e.g. a period match), and
  the CPU confirms just those.
* **Host interface.** The UART (2 Mbaud) is enough for loading, CRC runs and
  spot checks. It is not enough to stream every value (about 100k values/s).
  The full core would need PCIe/DMA (1 byte per value: about 230 MB/s).
* **Clock of the full core.** The loop could be cut down to a couple of
  comparisons. Register the first three free common values z1 < z2 < z3 of the
  mask without the terms a = 0, 1 (two cycles ahead). Then G(n) is the first of
  them that is neither G(n−1) nor G(n−2) (r₁ = G(1) = 0). The OR tree would
  also need a fourth stage. Both changes are untried.

## Vivado project

```
cd fpga
make project    # vivado/officers.xpr: Nexys A7-100T, folded core + UART (create_project.tcl)
make bit        # synthesis, implementation, bitstream (build_bitstream.tcl)
make program    # load it over JTAG (volatile)
make board      # run_board.py: load, stream 100k, CRC 10^7, compare with octal
make run N=1e10 # long run without checking (display: highest nim-value)
```

`create_project.tcl` generates the cores and test vectors into `build/gen/`, so
`fpga/vivado/` is not under version control. The project has two simulation
sets:

* `sim_1` (`tb_folded`): the folded core.
* `sim_full` (`tb_officers`): the full core. It only simulates, because it is
  too large for the 100T.

`build_bitstream.tcl` always starts from synthesis. It fails, rather than
reusing old results, in these cases:

* a killed run cannot be reset;
* a step does not complete;
* timing is not met. With `-tclargs keep` the bitstream is kept anyway, with a
  warning.

## Files

| file | |
|---|---|
| `fpga/gen_officers.py` | generates `build/officers_core.sv` and `officers_params.json` from octal's values |
| `fpga/model.py` | cycle-accurate Python model of the full core |
| `fpga/tb_officers.sv`, `fpga/sim.sh` | xsim testbench and runner (full core) |
| `fpga/gen_folded.py` | generates the folded core `officers_folded.sv`, `folded_info.svh`, `folded_params.json` |
| `fpga/tb_folded.sv`, `fpga/sim_folded.sh` | xsim testbench and runner (folded core, random stalls) |
| `fpga/ffs256.sv` | radix-4 find-first-set (full core, v1 folded core) |
| `fpga/nexys_a7_top.sv`, `fpga/uart.sv`, `fpga/nexys_a7.xdc` | board top level, UART and FIFO, pins |
| `fpga/create_project.tcl`, `build_bitstream.tcl`, `program.tcl` | Vivado project, bitstream, JTAG |
| `fpga/make_vectors.py` | test vectors for the project's simulation sets |
| `fpga/run_board.py` | host side: load, stream, CRC run, compare with octal; `--no-check` long runs |
| `fpga/synth.tcl` | out-of-context synthesis (+ implementation) of the full core |
| `fpga/Makefile` | `model`, `sim`, `sim-folded`, `synth`, `impl` (`PART=`, `MHZ=`), `project`, `bit`, `program`, `board` |

Vivado 2026.1 and xsim still load `libncurses.so.5` and `libtinfo.so.5`. The
Makefile and `sim.sh` therefore point `LD_LIBRARY_PATH` at a private directory
of symlinks to the `.so.6` libraries, and system files are not touched.
