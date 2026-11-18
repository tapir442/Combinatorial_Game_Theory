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
| `D` | dump the history → 20,627 × u16, G(n−20,627 … n−1) oldest first, then `d`; S and C continue afterwards |
| `M` v:u32 | raise the display maximum to v → `m` |
| `K` | period search: the current window's fingerprint becomes the checkpoint; clears the hit and the count → `k` fp:u64 |
| `H` | period search status → `h` fp:u64 chk:u64 since:u64 flags:u8 hit_at:u64 (flags: bit 0 hit, bit 1 checkpoint set; since = values computed since `K`; hit_at = the period, see *Period search*) |

`S` and `C` continue from where the previous command stopped. The LEDs show:

* 0: busy
* 1: error
* 2: MMCM locked
* 3: period-search hit
* 15–8: progress

The 7-segment display shows the **highest nim-value so far**, preloaded or
computed, in decimal. Each `L` clears it; `M v` (reply `m`) raises it to v. After
a reload the board only sees the 20627-value window, so `run_board.py --no-check`
sends the run's maximum from the state file with `M`.

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

Each `c` reply also carries the block's maximum and where it first occurs, so a
long run logs every new overall maximum, e.g. `new maximum: G(19,739,543) = 325`
(confirmed by octal: the first 325 and the maximum up to there).

**Checkpoints.** The board needs only its history, the last 20,627 values. `D`
dumps it in 0.2 s. The history shift register is rotated once around through the
fast-load path: the core outputs its oldest entry (`tail`), and the top feeds it
back in, 3 clocks per value. The engine is frozen meanwhile, and the history ends
as it started, so a run continues after a dump. `L` with any two values in front
of a dump restores the state (the first two only pass through). In check mode,
`run_board.py` compares the dump with octal. It then runs 10⁶ values twice, once
straight after the dump and once after reloading it, and both CRCs must match octal.

With `--no-check`, the state (next n, history, maximum so far and its heap) is
saved to `fpga/build/board-state.bin` (41 KB) after every block. The next run
continues from it, unless `--fresh` is given, so runs can go past where octal can
produce the preload. The first Ctrl-C stops after the block in flight and saves;
a second one aborts. `--checkpoint DIR` keeps a copy per block.

**Service.** `fpga/officers-board.service` (systemd user service; `make -C fpga
service-install`) runs `fpga/board_service.sh`. That script keeps the run going:

* If no board is present, it retries every 5 minutes.
* If the board does not answer (e.g. after a power cycle), it programs it over
  JTAG.
* It runs `run_board.py --no-check --total 0` from the state file.
* After an error or an unplugged board, it retries in 5 minutes.
* After `err` (exit 3), it stops for good.

`systemctl --user stop officers-board` sends SIGINT, so the block finishes and the
state is saved. Output goes to the journal and to `fpga/build/board-run.log`. The
serial port needs permanent access (group `dialout`, or a udev rule such as
`SUBSYSTEM=="tty", ATTRS{serial}=="210292BF7051", GROUP="plugdev", MODE="0660"`),
because a manual ACL is lost when the board is replugged.

Programming uses JTAG only (`make program`), so the design is gone after a power
cycle.

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
* **A split check fits, but barely pays.** The first idea was to check each heap
  against the first K heaps with fixed taps (the tap structure of the
  speculation core) and to hand the heaps it cannot settle to the PC. That
  fails, because the step-2 witnesses of Officers are deep (*Witness depth*,
  below). Lanes that stream the pairs out of block RAM and stop at the last
  witness, with DDR2 for the rare deep heaps, would fit. But beside the
  speculation core they verified only about 4 M values/s, about as fast as
  `octal --rare` on 24 threads (*Split check with lanes*, below). Cheaper
  lanes in LUT-RAM roughly double that, to about 8.7 M/s (*Cheaper lanes*, below).
* **Host interface.** The UART (2 Mbaud) is enough for loading, CRC runs and
  spot checks. It is not enough to stream every value (about 100k values/s).
  The full core would need PCIe/DMA (1 byte per value: about 230 MB/s).
* **Clock of the full core.** The loop could be cut down to a couple of
  comparisons. Register the first three free common values z1 < z2 < z3 of the
  mask without the terms a = 0, 1 (two cycles ahead). Then G(n) is the first of
  them that is neither G(n−1) nor G(n−2) (r₁ = G(1) = 0). The OR tree would
  also need a fourth stage. Both changes are untried.

### Witness depth

`fpga/witness_depth.c` recomputes G as `octal --rare` does (its CRC matches octal up
to 10⁸). For each heap n it records the **witness depth**: the pair position a at which
step 2 finds the last missing rare value below the speculated value. Hence a split
check with K taps settles exactly the heaps of depth ≤ K.

```
gcc -O3 -march=native -o build/witness_depth witness_depth.c
build/witness_depth 1e8       # 8.5 min
```

| n | heaps | mean | max (at n) | > 1,000 | > 4,096 | > 8,192 | > 16,384 |
|---|---:|---:|---:|---:|---:|---:|---:|
| 5·10⁴ – 10⁵ | 5·10⁴ | 2,757 | 38,392 (95,652) | 90 % | 19 % | 0.26 % | 0.002 % |
| 10⁵ – 10⁶ | 9·10⁵ | 2,753 | 45,131 (459,575) | 90 % | 19 % | 0.24 % | 0.004 % |
| 10⁶ – 10⁷ | 9·10⁶ | 2,747 | 431,340 (1,697,097) | 90 % | 19 % | 0.23 % | 0.005 % |
| 10⁷ – 10⁸ | 9·10⁷ | 2,749 | 615,713 (14,309,759) | 90 % | 19 % | 0.23 % | 0.004 % |

* **The bulk is stationary.** The mean (about 2,750, the same as octal's step-2 pair count) and
  the shares do not change with n.
* **The tail grows.** The deepest witness grows from 45k below 10⁶ to 616k at
  n ≈ 1.4 · 10⁷.

For the Nexys A7 this means:

* Small K is useless: with K = 1,000, 90 % of the heaps go to the PC.
* Flagging 0.2 % takes about 8,000 taps. That is 5 times the 1,584-tap core, which
  already uses 64 % of the LUTs, so it fits only with about 5 times the folding
  (≈ 5 M values/s).
* Even then the PC would have to finish 10–60k heaps/s. Each of those needs values
  up to 600k heap sizes back, far beyond the board's 20,627-value history and even
  beyond its whole block RAM (about 540k 9-bit values), and the UART carries about
  100k values/s.

This rules out fixed taps with the PC finishing the rest. Two things were
overlooked here, though, and the next section takes them up: the folded core
uses none of the 135 block RAMs (its history is LUT-RAM), and the board has
128 MiB of DDR2.

### Split check with lanes

`fpga/split_model.c` recomputes G like `witness_depth.c` (same CRC, `121478ad` for
G(50,000 … 10⁸ − 1)). It records per heap the work of the check and runs it through
three layouts:

```
gcc -O3 -march=native -Wall -o build/split_model split_model.c
build/split_model 1e8         # 11 min, 200 MB
```

**What the check is.** For n ≥ 50,000, step 1 never reaches a rare-class value
(0 times to 10⁸). The reason: every rare heap r is below 50,000, its partner
G(n−1−r) is common, and rare ⊕ common is common. So the check of heap n is all of
step 2. Every rare-class value below the speculated g ("need": mean 57.3, max 162)
must be some G(a) ⊕ G(n−1−a). The scan a = 1, 2, … stops at the witness depth.
The check may use the core's own speculated values: if every heap before n
passed, they are all correct (induction).

**Layouts** (to 10⁸; "decoders" = pairs evaluated per clock over all lanes):

| layout | how | heaps per clock | at 200 MHz, 128 decoders |
|---|---|---|---:|
| A fixed taps | pairs 1 … K for every heap; deeper heaps go to a fallback | decoders / K | 3.1 M/s (K = 8,192) |
| B free lanes | each lane streams its own heap from BRAM and stops at its depth | ≈ decoders / 2,750 | 9.3 M/s |
| C lockstep groups | GS lanes on GS consecutive heaps share the BRAM reads; the group waits for its deepest heap | B × efficiency | 4.4 M/s (GS = 4) |

* **B is ideal but impossible.** Every lane would need its own read ports into
  the prefix G(1 …) and the window of recent values. That needs far more than
  135 BRAMs.
* **C shares the reads.** All lanes of a group read the same G(a), and their
  window values G(m₀+i−a) are consecutive, so one wide read serves them all.
  The price is the group maximum:

  | GS | 1 | 2 | 4 | 8 | 16 | 32 |
  |---|---:|---:|---:|---:|---:|---:|
  | mean group maximum | 2,749 | 3,612 | 4,443 | 5,157 | 5,820 | 6,431 |
  | efficiency | 1 | 0.76 | 0.62 | 0.53 | 0.47 | 0.43 |

* **Pairs per lane (W) hardly matter.** For a fixed number of decoders,
  throughput changes by under 1 % between 1 and 8 pairs per lane and clock.

**The deep tail goes to DDR2.** Lanes read a BRAM window. A heap whose window
span (its depth, plus the heaps produced while it waits and runs) exceeds the
window needs a fallback:

| window | heaps beyond it | fallback pairs per heap |
|---:|---:|---:|
| 16,384 | 4.5 · 10⁻⁵ | 0.62 |
| 32,768 | 8.3 · 10⁻⁶ | 0.35 |
| 65,536 | 3.2 · 10⁻⁶ | 0.18 |

A 64k window is 16 RAMB36 per copy. At 4 M heaps/s about 13 heaps/s fall through,
needing about 0.7 M pairs/s in all. One slow lane reading from DDR2 handles that.
So the deep witnesses that ruled out the PC fallback are no problem on the board.

**Area is the limit.** `fpga/check_group.sv` is a first cut of one lockstep group
(not connected yet). `fpga/synth_check.tcl` places and routes it out of context on
the xc7a100t-1 at 200 MHz:

| group | LUTs | FFs | WNS | LUTs per decoder |
|---|---:|---:|---:|---:|
| 16 lanes × 1 pair | 9,645 | 4,278 | +0.257 ns | 603 |
| 16 lanes × 2 pairs | 12,463 | 4,438 | +0.101 ns | 390 |
| 4 lanes × 2 pairs | 3,517 | 1,121 | +0.461 ns | 440 |

A lane costs about 430 LUTs fixed, mostly its 256-bit set of missing values and
the test for empty. Each pair per clock adds about 175 LUTs. About 22k LUTs are
free beside the speculation core (P = 8). Groups of 4 lanes × 8 pairs (≈ 7.3k LUTs
each) give three groups and 96 decoders. That is 96 × 0.62 / 2,750 heaps per clock
≈ **4 M values/s**, the speed of `octal --rare` on 24 threads (≈ 3.8 M/s). Folding
the speculation core to P = 16 would free LUTs for perhaps 6 M/s. That is still
only 9 months to Grossman's 1.4 · 10¹⁴. These figures are extrapolated from the
three builds; the W = 8 group has not been built.

So for now verification stays on the CPU, and the board stays a speculation and
search engine (long runs, maxima, and the period search below). A board check is worth
building only with a much cheaper lane. Most of a lane's area goes to the
256-bit missing set, so that is where to look first. The next section does that.

### Cheaper lanes: LUT-RAM and a counter

`fpga/check_group_lr.sv` changes how a lane keeps track of the values.

* **Seen, not missing.** The lane keeps the values it has *seen*, as 256 bits
  of LUT-RAM (32 words of 8 bits). Each pair does one read-modify-write:
  asynchronous read, write on the clock edge, no forwarding needed.
* **A counter instead of a test for empty.** `left` starts at R(g), the number of
  rare-class values below g. It counts down on every newly seen one, and the lane
  is done at 0.
* **Self-clearing.** At the end of a heap the set holds exactly the values below g.
  The lane clears it in 32 clocks, usually while the rest of its group is still
  scanning.
* **The rare-G(a) test once per group.** A pair is rare-class exactly when
  G(a) is common, given that G(m−a) is common, which holds for n ≥ 100,000.
  So the test is done once on the broadcast G(a), not in every lane.

Three changes were needed to meet 200 MHz:
* R(g) as a constant table: computed from g it becomes a 512-input adder tree;
* the read address without the clear mux, using a simple-dual-port RAM;
* a write enable that does not wait for the read. Writing `word | bit` back
  over a set bit changes nothing.

| group (lanes only) | LUTs (LUT-RAM) | FFs | WNS |
|---:|---:|---:|---:|
| 4 lanes | 239 (24) | 182 | +0.236 ns |
| 16 lanes | 897 (96) | 630 | +0.077 ns |
| 64 lanes | 3,568 (384) | 2,410 | +0.000 ns |

**About 56 LUTs per lane**, against about 430 for `check_group.sv`.

**Feeding the groups.** With lanes this cheap there are tens of groups, and each
needs its own stream of G(a) and G(m−a). Narrow block-RAM ports would run out.
`fpga/check_cluster.sv` therefore has the following parts:

* **Wide memories.** The prefix G(0 … KP−1) and a ring of the last KW values
  are stored in words of 16 values (144 bits). Writes use 9-bit byte enables.
* **Time slots.** Each memory has one read port, shared by NS ≤ 13 groups.
  Group s gets a word every NS clocks.
* **Stream readers** (`fpga/check_stream.sv`). There are two per group, one
  going up the prefix and one going down the window. Each keeps two words in
  ping-pong and offers one value per clock.
* **Window shift register.** A shift register of GS values turns the window
  stream into the lanes' G(m₀+l−a).
* **Jobs and results.** A job is GS heaps with g and R(g), loaded into all lanes
  at once. A result is n₀ plus the lanes not done after LIM pairs; those heaps
  go to the fallback. A failed lane is aborted and clears itself.

Timing needed registered job acceptance, a registered count (`hit_q`), a
two-stage result mux, a registered abort, and the ping-pong buffers. The
buffers keep the wide enables off the path through the step decision.

`tb_check_cluster.sv` (`sim_check_cluster.sh`) runs the cluster on real G
values from octal. A heap must fail exactly when its witness depth is ≥ LIM:

| GS × NS | LIM | heaps from | failed (expected) | errors |
|---|---:|---:|---:|---:|
| 16 × 8 | 4,096 | 10⁶ | 69 (69) | 0 |
| 16 × 13 | 16,384 | 10⁶ | 0 (0) | 0 |
| 16 × 16 | 4,096 | 10⁶ | 469 (469) | 0 |
| 8 × 8 | 8,192 | 10⁵ | 1 (1) | 0 |
| 32 × 4 | 8,192 | 2·10⁶ | 0 (0) | 0 |

The 16 × 13 run takes 28.3 clocks per heap once the window is filled. The
model predicts 28.3: mean group maximum 5,820, plus about 70 clocks per job,
over 208 lanes. `tb_check_group_lr.sv` checks each lane alone: `done` comes
exactly 4 clocks after the model's witness depth (1,724 heaps, GS = 1, 4, 16).

Cluster out of context (KP = 16,384, KW = 32,768, LIM = 16,384; 12 RAMB36 each):

| GS × NS | lanes | LUTs (LUT-RAM) | FFs | WNS | LUTs per group |
|---|---:|---:|---:|---:|---:|
| 16 × 8 | 128 | 9,744 (776) | 12,315 | +0.183 ns | |
| 16 × 12 | 192 | 14,564 (1,168) | 18,264 | +0.132 ns | 1,205 (marginal) |
| 8 × 12 | 96 | 8,970 (592) | 13,500 | +0.220 ns | ≈ 750 |

A group thus costs its lanes plus **about 300 LUTs** for the streams (two 32:1
value muxes), the shift register and the state machine.

**What it gives.** About 20k LUTs are free beside the speculation core (P = 8):

| GS | LUTs per group | groups | lanes | verified heaps/s at 200 MHz |
|---:|---:|---:|---:|---:|
| 8 | ≈ 750 | 26 | 208 | ≈ 8.0 M |
| 16 | 1,205 | 16 | 256 | ≈ 8.7 M |
| 32 | ≈ 2,100 (not built) | 9 | 288 | ≈ 8.8 M |

That is about twice the old design (4 M/s) and 2.3 times `octal --rare` on 24
threads, so Grossman's 1.4·10¹⁴ would take about 6 months on the board.
What the estimate leaves out:
* the job builder, which computes R(g) from the core's values and writes the ring;
* the fallback for the heaps beyond LIM (about 5·10⁻⁵ of the heaps);
* routing in a chip whose slices are already 92 % occupied.

**Folding the speculation core further** frees LUTs for the check.
`fpga/synth_fold.tcl` generates the core for another P and places and routes the
whole board design (core, UART, period search) at 200 MHz, without touching the
board project:

| P | values/s | slots | LUTs (logic + memory) | FFs | slices | WNS | free LUTs |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 8 | 25 M | 198 | 41,190 (28,130 + 13,060) | 46,577 | 92 % | +0.280 ns | 22.2k |
| 12 | 16.7 M | 132 | 34,948 (21,874 + 13,074) | 37,484 | 79 % | +0.104 ns | 28.5k |
| 16 | 12.5 M | 99 | 29,757 (16,692 + 13,065) | 32,556 | 76 % | +0.124 ns | 33.6k |

Only the tap logic shrinks. The history (13k LUTs of shift registers) stays,
and so does the LUT-RAM left for the lanes: about 5.9k LUTs, enough for about
950 lanes. With 16-lane groups at 1,205 LUTs each:

| P | LUTs for the check | groups | verified heaps/s | limited by |
|---:|---:|---:|---:|---|
| 8 | 20k | 16 | ≈ 8.7 M | check |
| 12 | 25k | 20 | ≈ 10.9 M | check |
| 16 | 24k (85 % of the chip in all) | 20 | ≈ 10.9 M | check |
| 16 | 28k (91 %) | 23 | ≈ 12.5 M | core |

So P = 16 lets the check nearly keep up with the core: 11–12.5 M verified values
per second, depending on how full the chip can be routed. That is 3 times
`octal --rare`, and 1.4·10¹⁴ in 4–5 months on the board alone.

**The job builder** (`fpga/check_top.sv`) connects the clusters to the core.

* **Inputs.** It takes the top level's value events (`ev`, `comp`, `v`), with
  `restart`/`n_set` at a load.
* **Ring writes.** Every value goes into the clusters' window rings.
* **Jobs.** Every computed value goes, with R(g), into the job being assembled.
  A complete job goes to the first cluster with an idle group.
* **Stall.** The builder raises `stall` for the core while a job is pending, or
  while the ring would overrun the oldest running job.
* **Results.** It keeps a *verified frontier*: all heaps below it are settled
  or in the fail list. Jobs finish out of order, so it uses a ring of 64 done
  bits.
* **Fail list.** A FIFO of (n₀, mask) for the fallback, and a sticky `err`.

`tb_check_top.sv` (`sim_check_top.sh`) feeds it like the core does. First the
load of 20,627 values, then one computed value every PCORE clocks, which stops
while `stall` is high (seen 3 clocks late). At the end the frontier must be
complete, the fail list must be exactly the heaps of depth ≥ LIM, and `err`
must be 0:

| clusters × groups | LIM | core | heaps | flagged (expected) | clocks/heap | core stalled |
|---|---:|---:|---:|---:|---:|---:|
| 2 × 4 | 4,096 | 1/16 | 512 | 69 (69) | 36.5 | 34 % |
| 2 × 4 | 2,048 | 1/16 | 2,048 | 1,626 (1,626) | 17.5 | 3 % |
| 2 × 10 | 16,384 | 1/16 | 4,096 | 0 (0) | 21.5 | 18 % |
| 1 × 6 | 16,384 | 1/8 | 1,024 | 0 (0) | 65.1 | 74 % |

The 2 × 10 configuration is the one sized for P = 16: 320 lanes. Out of
context it takes **24,688 LUTs (1,944 LUT-RAM), 31,789 FFs and 25 RAMB36, at
WNS +0.078 ns**. Timing needed the following changes:
* the all-done flag and the ring-lag stall test registered;
* `job_rdy` as a register of its own;
* the FILL/SCAN states as flags of their own;
* the stream head's enable as `take` alone;
* the fail FIFO's full flag registered.

The paths left are mostly routing, not logic. Beside the P = 16 core
(29,757 LUTs) the total would be about 54.4k LUTs (86 %), 15.0k LUT-RAM (79 %)
and 34 BRAMs. That is ≈ 10.9 M verified heaps/s, against 12.5 M/s from the core.
The 4,096-heap simulation includes start-up and gives 9.3 M/s.

**Still open: the fallback.** Heaps beyond LIM = 16,384 pairs are about 4.5·10⁻⁵
of all heaps, so about 500 per second. Each needs pairs further down than any
on-board RAM holds: the deepest witness to 10⁸ is at a = 615,713.

## Period search

The folded core's next value depends only on its history window, the last
DEPTH = 20,627 values (the largest rare heap). If the window at n equals the window
at an earlier checkpoint n_c, the speculated sequence repeats from n_c with period
p = n − n_c, forever. The board looks for that while it runs. It does not slow the
core.

**The fingerprint.** `window_fp.sv` reads a 9-bit value w as a polynomial over GF(2).
It keeps

  fp = Σⱼ wⱼ(x) · x^(9j) mod P,  w₀ = G(n−1), …, w_{D−1} = G(n−D),

with P = x⁶⁴ + `0x1028ecf30eb3f40d`, irreducible and chosen at random (seed 20261118;
Rabin's test). Two different windows collide only if P divides their difference
(about 9D / 2⁶⁴ ≈ 10⁻¹⁴, and no swap or burst pattern is special). When v enters
and `old` leaves the window:

  fp′ = fp · x⁹ ⊕ v ⊕ old · x^(9D) mod P

That is a fixed XOR network: the 9 constants x^(9D+i) mod P are computed at
elaboration, and there are no carries or DSPs. The module gets `old` from its own
copy of the window in block RAM, a ring of 20,627 × 9 bits fed by the top level's
one event per history shift (computed values, loads, dump rotations). So it does not
depend on the core's timing. fp and the ring start as zeros and are never reset
separately, so fp is always the fingerprint of the ring. After an `L` the ring equals
the core's window.

**The check.** After every computed value the top level compares fp with the
checkpoint. The first match is latched: `hit_at` is the number of values computed
since `K`, i.e. the period. The hit stays until the next `K`, and the core runs on.

**The host** (`run_board.py --no-check`, so the board service does all of this):

* At the start of a session: after the load, the board's fingerprint must equal the
  one computed in Python from the loaded window. Then `K`, which sets the checkpoint
  n_c = the start heap.
* After every block: the fingerprint must equal Python's fingerprint of the dump.
  This tests the hardware about every 40 s; a difference exits with 4.
* Brent: once n ≥ 2 n_c, the checkpoint moves to n. So a period is found once the
  preperiod lies below a checkpoint, within about twice the time it takes to run
  through the period.
* On a hit with period p: dump W(n), run exactly p more values, dump W(n + p) and
  compare value by value. If they are equal, the speculated sequence is periodic:
  the run prints `PERIOD CONFIRMED`, saves and exits with 5, and the service does not
  restart. Otherwise it was a fingerprint collision, and a new checkpoint is set.

A confirmed period of the *speculated* values is a candidate only. It becomes a
result with the values verified (`octal --rare`) over the range that the periodicity
theorem for octal games asks for. A restart sets a new checkpoint at the restart heap
(the board cannot be given an old one); that only delays the Brent schedule.

**Tests.**

* `sim_window_fp.sh`: xsim of `window_fp` with 63,115 random events, partly back to
  back. `check_window_fp.py` checks every fingerprint against the rolling formula
  and about 60 of them against the window recomputed from scratch.
* `run_board.py` (check mode), on the board:
  * the fingerprint after the load and after a dump, against Python;
  * `K` at W(n), then reload W(n−1) and compute one value: the window is W(n) again,
    so there must be a hit with period 1.

  Both passed on 2026-11-18, together with the stream, CRC, dump and resume checks.

**Cost.** The build with the period search meets 200 MHz with WNS +0.280 ns, and
uses 41,190 LUTs (65 %) and 9 block-RAM tiles (the ring). The critical path is
back inside the core. The long run resumed with it at n = 5.27·10¹², which is
also its first checkpoint.

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
| `fpga/run_board.py` | host side: load, stream, CRC run, dump, compare with octal; `--no-check` long runs with saved state |
| `fpga/board_service.sh`, `fpga/officers-board.service` | systemd user service that keeps the long run going |
| `fpga/witness_depth.c` | step-2 witness depth per heap (CPU), for sizing a check on the FPGA |
| `fpga/window_fp.sv` | period search: block-RAM copy of the window and its 64-bit fingerprint |
| `fpga/tb_window_fp.sv`, `fpga/check_window_fp.py`, `fpga/sim_window_fp.sh` | xsim test of `window_fp` against Python |
| `fpga/split_model.c` | model of an on-board check: fixed taps, free lanes, lockstep groups; tail beyond a BRAM window |
| `fpga/check_group.sv` | one lockstep group of check lanes (first cut, for area and clock estimates) |
| `fpga/check_group_lr.sv` | lockstep group of cheaper lanes: seen set in LUT-RAM, counter, self-clear |
| `fpga/check_stream.sv` | one value per clock out of a wide block RAM shared in time slots |
| `fpga/check_top.sv` | job builder + clusters: jobs from the core's values, stall, verified frontier, fail list |
| `fpga/tb_check_top.sv`, `fpga/sim_check_top.sh` | xsim: check_top fed like the core (with stalls), against the model |
| `fpga/check_cluster.sv` | NS lane groups with prefix and window RAMs, jobs in, unsettled heaps out |
| `fpga/tb_check_group_lr.sv`, `fpga/sim_check_group_lr.sh` | xsim: each lane's done against the model's witness depth |
| `fpga/tb_check_cluster.sv`, `fpga/sim_check_cluster.sh` | xsim: the cluster's failed heaps against depth ≥ LIM |
| `fpga/gen_check_vectors.py` | test vectors: G from octal and the witness depth of each heap |
| `fpga/synth_fold.tcl` | place and route of the whole board design for another fold P (no bitstream) |
| `fpga/synth_check.tcl` | out-of-context place and route of `check_group`, `check_group_lr` or `check_cluster` on the A7 part |
| `fpga/synth.tcl` | out-of-context synthesis (+ implementation) of the full core |
| `fpga/Makefile` | `model`, `sim`, `sim-folded`, `synth`, `impl` (`PART=`, `MHZ=`), `project`, `bit`, `program`, `board` |

Vivado 2026.1 and xsim still load `libncurses.so.5` and `libtinfo.so.5`. The
Makefile and `sim.sh` therefore point `LD_LIBRARY_PATH` at a private directory
of symlinks to the `.so.6` libraries, and system files are not touched.
