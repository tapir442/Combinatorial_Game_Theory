# Idea: computing octal nim-sequences on multi-core CPU, GPU or FPGA

Status:
- **Multi-core CPU (section 1): implemented.** `octal --blocked -j N`, `octal_extend_blocked()`,
  Python `method="blocked"`.  Measured on .007, N = 10⁶, 12 cores: 143 s brute force → 9.5 s (15×).
- **GPU (section 2): implemented with Vulkan and ROCm, not CUDA.** `octal --gpu` / `--rocm`,
  `octal_extend_gpu()` / `octal_extend_rocm()`, Python `method="gpu"` / `"rocm"`.  The only GPU
  available was the Ryzen 9 7900X iGPU (2 CUs): 23.3 s (Vulkan) and 20.8 s (ROCm, wave64) for the
  same run, about 3.5× one core but slower than 24 CPU threads.  Not yet measured on a discrete GPU.
- **FPGA (section 3): idea only.**  Its figures are back-of-envelope estimates, not measurements.

## Why it does not parallelise naively

Almost all the work for an octal game is the two-heap pair scan:

```
for a in 1..m/2:  mark G[a] ^ G[m-a]      (m = n-k for each allowed k)
G(n) = mex(marked)
```

That is O(n) per heap size and O(N²) in total.

- **Within one n** the scan is very parallel: independent XORs ORed into a
  bitset of reachable values.
- **Across n** it is strictly sequential, because G(n) needs all earlier values.
  One GPU kernel launch per n (about 5 µs overhead) costs more than the work
  itself.

## Key step: blocked computation

Take a block of B heap sizes [n₀, n₀+B). For a pair a + b = m with a ≤ b:

- **Both a, b < n₀**: these values are already known. That is almost all pairs,
  and they can be done in parallel for all B heaps at once (about B·n/2 XORs).
- **b ≥ n₀**: then a = m − b < B. That leaves only about B² small pairs,
  finished sequentially inside the block in order of n.

The bulk becomes one large parallel job per block, and only a small sequential
tail is left. This works for any octal code, and it is also what makes a
multithreaded/SIMD CPU version possible.

## 1. Multi-core CPU (do this first)

- The current `octal.c` computation is single-threaded.
- Blocked algorithm + threads + AVX2: estimated 10–30× on a desktop.
- A modest change to `octal.c` that keeps the `octal_extend()` API.

## 2. GPU (CUDA, or CuPy for a prototype)

Effort: roughly days.

- `G` as uint16 in device memory (10⁸ values = 200 MB).
- Per block: thread blocks own a few heap sizes, stream `G[a]` forwards and
  `G[m-a]` backwards (both reads are coalesced), and OR into a reachable-values
  bitset in shared memory (512–4096 bits, depending on the maximum value). A
  short sequential kernel finishes the B² tail and takes the mex.
- Estimate: N = 10⁸ needs about N²/4 ≈ 2.5·10¹⁵ XORs. At about 10¹³ simple
  ops/s that is minutes on a GPU, against hours to days on one CPU core.
- Catches:
  - Large nim-values need large bitsets, which puts pressure on shared memory.
  - The `--sparse` early exit (stop once the mex is "rare") parallelises badly,
    so the GPU version uses brute force.
  - Hexadecimal three-heap moves are O(n²) per n: still parallel, but heavier.

## 3. FPGA

Effort: weeks to months, plus hardware.

- For heaps m = n₀+i, the partners `G[m−a]` form a sliding window, so:
  - stream `G[a]` in one value per clock, and the reversed stream through a
    **shift register of length B**;
  - each clock, B units XOR the new `G[a]` with their window entry and set one
    bit in their own bitset register;
  - a priority encoder takes the mex at the end.
- 300 MHz × B ≈ 1000 gives about 3·10¹¹ XORs/s, which is GPU class.
- Limits: logic resources (B bitsets of up to vcap bits each) and DDR bandwidth
  once G no longer fits on chip.
- Needs HLS or Verilog, verification against the C reference, and toolchain
  work. Interesting as a research project, but unlikely to beat a GPU on cost
  or development time.

## Recommendation

1. Blocked algorithm on the CPU (threads + SIMD).
2. Port the same blocked kernel to CUDA or CuPy if N ≥ 10⁸ is needed.
3. FPGA only if the hardware design itself is the point.

Validate every variant against the existing brute-force `octal_extend()`, for
example on .007 and .6.
