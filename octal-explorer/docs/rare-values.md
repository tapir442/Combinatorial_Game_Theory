# Rare-value algorithm (Grossman)

`--rare` (C: `octal_extend_rare`, Python: `method="rare"`) implements the *rare-value
algorithm* as described by J.P. Grossman, **Searching for periodicity in Officers**, *Games of
No Chance 5*, MSRI Publications 70 (2017), 373–385
([PDF](https://library.slmath.org/books/Book70/files/1016.pdf)).  Grossman credits the
algorithm to *Winning Ways*.  He used it, with further optimisations, to compute more than
140 trillion (≈ 2⁴⁷) values of Officers (`.6`) without finding a period.

The method is **exact**: it gives the same values as brute force for every game and every
mask.  How fast it is depends on the game.

## Rare and common values

Fix a bit mask \(m\).  A value \(v\) is **rare** if \(\operatorname{popcount}(v \mathbin{\&} m)\)
is **even**, and **common** if it is odd.  The parity of \(v \mathbin{\&} m\) respects XOR, so

| ⊕ | rare | common |
|---|---|---|
| **rare** | rare | common |
| **common** | common | rare |

The rare values form a group under XOR (0 is always rare), and the common values are its
other coset.  For Officers, with \(m\) = bits 1, 2, 3, 5, 6, 7, 8 (`0x1ee`), only
**1584 heap sizes** have a rare value; the last one is \(G(20627) = 277\).  All the other
values, at least up to 2⁴⁷, are common.

## The algorithm

For a heap of size \(n\), with the list of rare heap sizes so far:

1. **Candidate.**  A common value needs a pair with exactly one rare heap.  So looping over
   the rare heap sizes \(a\) and marking \(G(a) \oplus G(m - a)\) finds *every* reachable
   common value.  The smallest unmarked common value, `cval`, is therefore certainly not
   reachable.  This costs one step per rare heap size, independent of \(n\).
2. **Witnesses.**  \(G(n) = \)`cval` unless a rare value below `cval` is unreachable.  Almost
   every pair is common ⊕ common, which is rare, so rare values are reached quickly.  Scan the
   pairs, count down the unmarked rare values below `cval`, and **stop when none is left**.
   If the scan ends with one still unmarked, the smallest such value is \(G(n)\): a new rare
   value, which is appended to the list.

The 0-, 1- and 3-heap options are marked in full, as in brute force (`mark_simple`).  Step 2
is O(n) in the worst case, but usually stops after a few thousand pairs, so the cost per heap
size is almost constant instead of growing with \(n\).

## Why the older `--sparse` mode is not faster

`--sparse` (see [Internals](c-internals.md#the-sparse-mode)) uses the opposite convention:
there a value is rare when the parity is **odd**.  Then step 1 finds every reachable *rare*
value, but \(G(n)\) is usually *common*, and accepting a common candidate needs a proof that
it is **not** reachable, which always takes the full O(n) scan.  The rare-value algorithm puts
the candidate in the class that step 1 enumerates completely, and only needs proofs that
values **are** reachable, which can stop early.

## Implementation

`extend_rare` in `octal.c`:

- uses brute force until a mask makes at most 5% of the heap sizes rare.  The mask is chosen
  (`rv_choose_mask`: the one with the fewest rare heap sizes, trying all masks below the value
  capacity) at \(n\) = 2048, 8192, 32768, …, and when a computation starts past 2048;
- then, with `-j 1`, runs `grundy_rare` (steps 1 and 2 above) for every heap size, appending
  each new rare heap size;
- otherwise (the default: all cores) uses **speculation**, see below;
- reports in `summary.txt` the mask, the number of rare heap sizes, the pairs examined in each
  step, how often step 2 had to scan everything, and the speculation statistics (values
  speculated, how many with one byte per value, failed checks).

## Speculation (Grossman §3.1)

Step 2 is about 60% of the work, and it almost never changes the result.  So `rare_spec_run`
works on chunks of \(n/8\) heap sizes (between 4096 and 2¹⁸, ending at the next mask checkpoint):

1. **Speculate** on one thread (`rare_speculate`; now on a small team, see *Baton passing*): compute every value of the chunk with step 1
   only, that is, assume that no new rare value appears.  Speculative values are always common.
2. **Check** in parallel (`rare_check_worker`): recompute every value of the chunk with the full
   algorithm, taking the speculative values before it as given.  Given those values, the heap
   sizes are independent, so the threads share them out in blocks of 64, each with its own
   mark table.
3. If every value agrees, all are correct, by induction over \(n\).  Otherwise let \(f\) be
   the first heap size that disagrees.  The values before \(f\) are correct, so the value the
   check computed for \(f\) is correct too: it is a new rare value.  It is stored and added to
   the rare list, and speculation resumes at \(f + 1\); the speculative values after \(f\)
   are thrown away.

**Overlap.**  Steps 1 and 2 run at the same time on consecutive chunks: while the other
threads check chunk A, thread 0 already speculates the next chunk B, assuming A is correct,
and then joins the check.  This is safe because checking A reads only values below the end of
A and speculating B writes only values from there on.  If A is correct, B becomes the next
chunk to check, overlapped with the speculation of the one after.  If A is wrong, B is thrown
away with the rest of A, and the next chunk is speculated on its own.  So the speculation,
which is sequential, runs almost without pause, and the check is hidden behind it.

**Effect of the overlap** (same machine, 24 threads, identical values; seconds):

| code | n | speculation, then check | overlapped |
|---|---:|---:|---:|
| `.6` | 10⁷ | 14.8 | 14.9 |
| `grundy` | 10⁷ | 12.1 | 11.4 |
| `couples` | 10⁷ | 16.1 | 12.8 |

The gain is small (0–20%), because the check was already cheap: spread over 23 threads it
costs much less than the speculation.  Timing the speculating thread for `.6` to 10⁷: 13.0 s
of the 15.4 s are speculation, and it waits for the check only 1.8 s in all (mostly at the
first chunk after each mask checkpoint, where there is nothing to overlap).  So the
**sequential speculation, about 1.3 µs per value, is now the bottleneck**; that is what
Grossman's remaining optimisations below address.

OpenMP's waiting policy matters: with `OMP_WAIT_POLICY=active` idle checking threads spin and
slow down the speculating thread on the same physical core (18.6 s instead of 14.9 s for `.6`
to 10⁷).  The default (spin briefly, then sleep) is fine; `OMP_WAIT_POLICY=passive` is slightly
faster (14.0 s).

## One byte per value (Grossman §3.2)

All Officers values are below 512.  Fix a bit \(j\) of the mask below 512 (the highest one) and
delete it from every value:
\(\operatorname{byte}(v) = (v \mathbin{\&} (2^j - 1)) \mid ((v \gg (j+1)) \ll j)\).  This map is
linear under XOR, and on the **common** values below 512 it is one-to-one onto 0..255, because
for a common value the parity condition fixes bit \(j\).  (Grossman deletes bit 8 for the
Officers mask `0x1ee`; that is the same rule.)

So the speculation keeps a byte copy `g8` of the values, a 256-entry mark table, and the rare
heap sizes with the bytes of their values (`RareBytes`, `rare_step1_bytes`).  Step 1 then reads
half as much memory: the last ~20,000 values it needs fit in the 32 KB L1 cache instead of
taking 40 KB.

On bytes, step 1 is only correct if every marked value is common, otherwise a rare and a common
value could share a byte.  A marked value \(G(a) \oplus G(m-a)\) with \(a\) rare is common unless
\(m - a\) is rare too, which cannot happen once \(n > 2 \cdot{}\)(largest rare heap size)\({} + t\),
for `.6` from \(n = 41{,}256\) on.  Before that, for codes with 3-heap moves, and as soon as a value
reaches 512, the speculation uses the 16-bit step 1.  The speculative values are therefore the
same either way, and the check still uses the full 16-bit algorithm.  A debug build that ran both
versions of step 1 for every speculated heap size of `.6`, `grundy`, `couples` and `.77` up to
2·10⁶ found no difference; for `.6` up to 10⁷, 99.9% of the speculated values used bytes.

Effect, same machine (seconds, 24 threads, identical values):

| code | n | 16-bit speculation | one byte per value |
|---|---:|---:|---:|
| `.6` | 10⁶ | 1.69 | 1.37 |
| `.6` | 10⁷ | 14.2 | 10.4 |
| `grundy` | 10⁷ | 11.8 | 8.9 |
| `couples` | 10⁷ | 13.1 | 10.3 |

The speculating thread for `.6` to 10⁷ now computes for 7.7 s instead of 13.0 s and waits for
the check 2.0 s, so the check starts to matter again.

## Baton passing (Grossman §3.3)

Most of step 1 for \(G(n)\) does not depend on the last few values.  So a **team** of \(P\)
threads speculates each chunk (`rare_baton_worker`, `rare_speculate_chunk`; byte mode only):
thread \(j\) takes the heap sizes \(n \equiv n_0 + j \pmod P\), and for each of them

1. **before the baton** marks every byte whose inputs are already final: the step-1 pairs
   \((a, m-a)\), \(m = n - k\), with partner \(m - a \le n - P\), i.e. rare \(a \ge P - k\), and the
   1-heap options \(n - k\) with \(k \ge P\).  These values are final because the thread's own
   previous heap size \(n - P\) already had the baton.  Then it takes the smallest unmarked
   common value as candidate (Grossman's **speculative mex**);
2. waits for the **baton**: a shared counter `upto` (all values below it are final) reaching
   \(n\) (acquire load; spinning with `pause`);
3. **after the baton** marks the rest: the few rare \(a < P - k\) and the 1-heap options with
   \(k < P\).  Marks only grow, so the value is the candidate unless the candidate just got
   marked; then the search continues from there;
4. stores \(G(n)\) and its byte and passes the baton (`upto = n + 1`, release store).

Steps 1 and 3 together mark exactly what the one-thread byte step 1 marks, so the speculative
values are identical (checked: same values and same restart counts as before for every team
size from 1 to 23).  On the critical path per value there is only the hand-over plus a handful
of marks.  If all 256 common bytes are reachable (the value would not fit), the thread with the
baton stops the team and thread 0 continues the chunk alone with the 16-bit step 1.

The team is threads 0 .. P−1 of the same parallel region in which the others check the previous
chunk; at most \(n_t - 1\) speculate, so at least one thread checks from the start.  The team
size is 6 (at most half the threads; 4 before the cheaper check below); `OCTAL_BATON=P` overrides it (`1` = one thread, as
before).  It is printed in `summary.txt`.

**Pipeline drains.**  With the speculation that much faster, the restarts of the pipeline began
to dominate: `compute()` used to call `extend_rare` once per \(n/8\) heap sizes (for the progress
display), and every call speculates its first chunk and checks its last one without overlap.
For `.6` to 10⁷ that was 3.6 s of 6.6 s.  `compute()` now passes the whole range in one call,
and `extend_rare` reports progress itself after each checked chunk.  (The mask checkpoints at
\(n = 4^k \cdot 2048\) still drain the pipeline.)

Effect for `.6` to 10⁷ (Ryzen 9 7900X, 24 threads, identical values; compute time in seconds):

| team size \(P\) | 1 | 2 | 4 | 6 | 8 | 12 |
|---|---:|---:|---:|---:|---:|---:|
| `extend_rare` per \(n/8\) | 9.87 | 7.09 | **5.82** | 6.04 | 6.28 | 7.31 |
| one call | 8.94 | – | **4.92** | 4.95 | – | – |

Without baton passing, the old code took 9.88 s.  With \(P = 4\) the team spends about 3.2 s of
the 4.9 s speculating; the regions in which it runs take 4.6 s, because the **check** of each
chunk (the full algorithm, 16-bit, on the remaining 20 threads) now takes longer than the
speculation.  So larger teams did not help, and the next step was a cheaper check (below).
Thread binding (`OMP_PROC_BIND`, `OMP_PLACES`) changed little (5.6–6.4 s).

## A cheaper check

The check recomputes every value with the full algorithm.  Two changes make it cheaper without
giving up its independence from the speculation (it still recomputes each value from \(G\)
alone, so it would also catch a mistake in the baton passing):

- **Step 1 on bytes** (`grundy_rare_bytes`): where the byte mode applies, the check runs the
  byte step 1 on its own 256-entry mark table, and then puts the common values below `cval`
  (all reachable, by step 1) and the 0-, 1- and 3-heap options into the 16-bit table for
  step 2.  In the byte range no step-1 pair has two rare heaps, so step 2 sees exactly the
  same marks as before.  Alone this gained only about 6% (4.9 → 4.6 s for `.6` to 10⁷),
  because per heap size step 1 examines about 1,580 pairs and step 2 about 2,740.
- **Vectors in the tail of step 2** (`rare_step2`, `RARE_SIMD`): step 2 spends most of its
  pairs on the last few missing rare values.  For `.6` up to 2·10⁶ it starts with 57 missing
  values on average; fewer than 8 are left after 840 pairs, fewer than 4 after 1,240, and the
  last one is found after 2,750 (`grundy`: 33 missing, ≤ 8 after 650 of 3,140).  So once at most
  8 values are missing, step 2 takes 16 pairs \((a+i,\ m-a-i)\) at once (GCC vector extensions;
  AVX2/AVX-512 with `-march=native`), XORs them, compares with each missing value, and only a
  block with a hit is gone through pair by pair.  Same marks, same result, and the same pair
  counts as the scalar loop (`-j 1` gives identical statistics).

Effect (seconds; values identical, also against `-j 1`, at 2·10⁶ for 1, 8 and 24 threads):

| | before | cheaper check, team 4 | team 6 |
|---|---:|---:|---:|
| `.6` 10⁷ | 4.96 | 3.5–4.1 | **3.13–3.18** |
| `grundy` 10⁷ | 4.95 | 3.8–4.2 | **3.54–3.66** |
| `couples` 10⁷ | 4.5–4.6 | 4.0–4.2 | **3.06–3.12** |
| `.6` 2·10⁶, `-j 1` | 12.3 | 7.5 | – |

The plain algorithm (`-j 1`) is 40% faster.  With the check cheaper, a larger team pays off
again: 6 and 5 are best, 8 is no better.

Grossman reached 5.2 million values per second with 8 threads (speculative mex), against about
3.2 million here.  His check ran on a separate cluster; here the same 24 threads do both.

## Generated step-1 code (Grossman §3.2)

Step 1 on bytes marks \(\mathit{rval}_i \oplus g8[m - \mathit{rpos}_i]\) for every rare entry
\(i\).  The rare list changes only when a new rare value appears, so Grossman generates code with
the entries as constants.  Here `rb_jit_update` writes the x86-64 machine code directly (no
compiler at run time), three instructions per rare entry:

```
movzx eax, byte [rdi - rpos]    ; g8[m - rpos]   (rdi = g8 + m)
xor   eax, rval                 ; left out if rval = 0
mov   [rsi + 4*rax], edx        ; mark[...] = n
```

into an anonymous mapping that is then made read-only and executable (`mprotect`).  It is
regenerated between parallel regions whenever the rare list or the mask changed, and covers the
entries with \(\mathit{rpos} \ge 16\), so the baton passing (team of at most 16) can still do
the few smaller ones after the baton.  Other architectures, `OCTAL_JIT=0`, or a failed `mmap`
fall back to the loop.  It is used by the speculation, the baton passing, the check, and now
also by `-j 1` (which before used the 16-bit step 1 throughout).

Microbenchmark (the 1,584 rare entries of `.6` at 10⁷): **0.19 ns per entry instead of 0.40**,
about one cycle, as Grossman's "about three instructions per rare heap" suggests.

Effect (seconds, identical values; `-j 1` also identical pair counts):

| | loop | generated code |
|---|---:|---:|
| `.6` 10⁷ | 3.07–3.12 | **2.57–2.67** |
| `grundy` 10⁷ | 3.51–3.62 | **3.13–3.18** |
| `couples` 10⁷ | 2.97–2.98 | **2.39–2.57** |
| `.6` 2·10⁶, `-j 1` | 7.5 (16-bit step 1) | **5.99** |
| `grundy` 2·10⁶, `-j 1` | – | 4.68 |

Team sizes 4–6 are now equal within noise (`.6`: 2.49, 2.39, 2.51 s; 8 is slower), so the
default stays 6.  The whole run is now limited by CPU work rather than baton latency: with 6
team threads and a single checking thread, the team speculates `.6` at 0.17 µs per value, but
at 0.25 µs when the checking threads share its cores.

## Failed stores

If storing a speculative value fails (a value above 65535, or out of memory), the chunk ends
there; when that heap size comes up again as the first of a chunk, it is computed exactly
instead, so an error is only reported for a correct value.

So speculation is **exact** as well.  Grossman checked the intervals on a cluster; here the
cores of one machine do it.  It starts at \(n = 4096\) once a mask is active; `-j 1` turns it
off.  For `grundy` up to 150,000 the check fails 12 times (12 new rare values), and the result
still equals brute force.

For Officers it finds exactly Grossman's mask `0x1ee` and his 1584 rare values (1583 heap
sizes ≥ 1, plus \(G(0)\)).  `--rare --check` and the tests compare it with brute force.

## Which games benefit

The method needs a game whose rare heap sizes stop appearing.  Rare heap sizes among
\(n < 200{,}000\) for the best mask:

| code | best mask | rare heap sizes | last rare heap | benefit |
|---|---|---:|---:|---|
| `.6` (Officers) | `0x1ee` | 1,583 (0.8%) | 20,627 | yes |
| `grundy` | `0xfe` | 1,272 (at 60,000) | | yes |
| `couples` | `0x1ee` | 1,584 (at 60,000) | | yes |
| `.007` (Treblecross) | `0x7b8` | 21,196 (10.6%) | 199,914 | no: falls back to brute force |
| `.164` | `0x1e60` | 77,499 (39%) | 199,999 | no |
| `.07`, `.137` | `0x9` | 82,355 (41%) | 199,997 | no (periodic; small values) |

| `.106` | (none) | about 50% for every mask | 999,999 | no, but see [sparse positions](#sparse-positions-flammenkamp-106) |

So the rare-value algorithm does **not** help Treblecross: about 10% of its heap sizes are
rare under the best mask, and new ones keep appearing.  Flammenkamp's computation of `.007`
to 2²⁵ must rely on something else.

## Sparse positions (Flammenkamp; `.106`)

Some games have a sparse **position** space instead of a sparse value space (type 1 in
[Flammenkamp's table](https://wwwhomes.uni-bielefeld.de/achim/octal.html); definition on his
[sparse-space page](https://wwwhomes.uni-bielefeld.de/achim/octal_sparse.html)).  For a code with
exactly one digit greater than 3, at index \(t\), heap size \(n\) is *sparse* if
\(\operatorname{popcount}(G(n) \mathbin{\&} m) \equiv n - t \pmod 2\).

Then every two-heap option has \(a + b = n - t\), and with \(q(x)\) the parity of
\(G(x) \mathbin{\&} m\), two non-sparse positions give \(q(a) \oplus q(b) = (a + b) \bmod 2 = (n - t)
\bmod 2\): exactly the class that would make \(n\) sparse.  So the values of the other class come
only from pairs with a sparse position: the rare-value algorithm applies, with the rare class
depending on the parity of \(n\).

`.106` (Flammenkamp 2002: period 328,226,140,474, preperiod 465,384,263,797; OEIS A071458) is such
a game.  Our values up to 10⁶ confirm his table: with \(m = 13\) (`0xd`) and \(t = 3\) there are
exactly **15 sparse positions** (3, 7, 10, 26, 68, 84, 96, 101, 320, 345, 359, 473, 563, 993, 1103),
the last at 1103.  Under every value mask about half of the heap sizes are rare, which is why
`--rare` used to fall back to brute force for `.106` (154 s for 10⁶).

**Position masks (implemented).**  The rare class of a value now depends on the heap size it
belongs to: for heap \(n\), \(v\) is rare iff \(\operatorname{popcount}(v \mathbin{\&} m)\) has
parity \(p(n)\), with \(p(n) = 0\) for a value mask and \(p(n) = (n + t) \bmod 2\) for a position
mask (`rv_rare`, flags `RV_POS`/`RV_PAR` in the mask).  Steps 1 and 2, the speculation and the
check only rely on "two heaps that are not rare give a rare XOR", which holds for a position mask
whenever every two-heap move removes a number of beans of the same parity (`rv_pos_parity`; no
three-heap moves).  `rv_choose_mask` tries both kinds and takes the one with the fewest rare heap
sizes.  The byte mode, generated code and baton passing are value-mask only; with 15 rare heap
sizes step 1 is short anyway.  `summary.txt` reports e.g.
`position mask 0xd (rare: parity of G(n) & mask = parity of n + 1), 15 rare heap sizes`.

Results for `.106` (identical to brute force up to 4·10⁵, and `-j 1` = `-j 24` up to 10⁷):

| n | `-j 1` | 24 threads | brute force |
|---:|---:|---:|---:|
| 4·10⁵ | – | 0.02 s | 22.6 s |
| 10⁶ | 0.07 s | 0.04 s | 154 s |
| 10⁷ | 0.67 s | 0.43 s | – |
| 10⁸ | 6.65 s | 4.06 s | – |

That is 15 million values per second on one core and 25 million with speculation (the
speculation itself is then the sequential limit).  Per heap size, step 1 examines 15 pairs and
step 2 about 16; the deepest witness pair up to 10⁸ is \(a = 8{,}388\) (at \(n \approx 4.3\cdot
10^7\)), consistent with Flammenkamp's depth of 15,343.  The largest value up to 10⁸ is 30.
Other codes now use position masks too, e.g. `.26` and `.34`.

Next:

1. (done, see above)
2. A sliding window: Flammenkamp (OEIS A071458) notes that the last 15,343 + 3 values and the
   15 sparse values suffice, instead of all 1.6·10¹² values (3.2 TB as 16-bit values).
3. Verify the period without storing the sequence: by Flammenkamp's criterion it suffices to
   check \(G(i + p) = G(i)\) for \(t + \max(\text{last}, \text{depth}) = 15{,}346\) successive
   values, i.e. compute to preperiod + period + 15,346 ≈ 7.9·10¹¹.  At the measured 15–25 million
   values per second that is about 9–15 hours on this machine, if the sliding window does not
   slow it down.

## Benchmark

`benchmarks/bench_methods.py` (results in `benchmarks/results/methods.md`) times every method
through the command-line program and checks that all give identical values.

Wall-clock seconds on a Ryzen 9 7900X (12 cores, 24 threads; GPU = its integrated Radeon).
`brute` and `rare -j 1` use one core; `blocked` and `rare` (speculation + parallel check) use
all 24 threads.

| code | n | brute | blocked | gpu (Vulkan) | rocm | rare -j 1 | **rare** |
|---|---:|---:|---:|---:|---:|---:|---:|
| `.6` | 10⁵ | 1.46 | 0.18 | 0.24 | 0.55 | 0.37 | 0.23 |
| `.6` | 3·10⁵ | 13.2 | 0.96 | 1.97 | 2.16 | 1.00 | **0.29** |
| `.6` | 10⁶ | – | 9.29 | 22.4 | 18.9 | 3.06 | **0.70** |
| `.6` | 10⁷ | – | – | – | – | 29.5 | **2.93** |
| `grundy` | 10⁶ | – | 8.82 | 22.2 | 19.4 | 2.47 | **0.65** |
| `grundy` | 10⁷ | – | – | – | – | 23.8 | **3.69** |
| `couples` | 10⁶ | – | 9.12 | 22.4 | 19.0 | 2.89 | **0.49** |
| `couples` | 10⁷ | – | – | – | – | 28.4 | **2.85** |
| `.007` | 3·10⁵ | 12.9 | 0.99 | 1.99 | 2.16 | 12.9 | 12.9 (falls back to brute) |
| `.007` | 10⁶ | – | 9.32 | 22.9 | 18.9 | – | – |

Below a second the `rare` times vary by about ±0.1 s from run to run (`.6` at 10⁶: 0.48–0.66 s
in repeated runs, with and without the generated code).  The full table, with all sizes, is in `benchmarks/results/methods.md`.

- For the three sparse games the plain rare-value algorithm runs at an almost constant
  **≈ 340,000–420,000 values per second** on one core (10⁶ → 10⁷ takes 10× as long, not 100×;
  170,000 before the vector tail of step 2 and the generated step-1 code).  Grossman reports 88,000 values/s for the same
  algorithm on a 2008 Xeon.
- **Speculation** with overlapping check, one byte per value, baton passing and the cheaper
  check makes it 6.5–10× faster again: **≈ 2.7–3.5 million values per second** at 10⁷ (wall
  clock including printing the values; the compute time alone is 2.4–3.2 s).  Grossman's speculation
  gave 2.2× (196,000 values/s), one byte per value 321,000 values/s, and with unrolled code and
  baton passing on 8 threads 5.2 million values/s (with the check on a separate cluster); here
  the check runs on the same 24 threads, at the same time as the speculation.
- At \(n = 10^6\), `rare` is 13–19× faster than `blocked` on all 24 threads, and about 200× faster
  than brute force (143 s for `.6`).  At \(10^7\) the O(n²) methods would need roughly 100× their
  10⁶ time (about 1000 s for `blocked`), so `rare` is then about 270–350× faster than `blocked`, and
  the gap keeps growing with \(n\).
- Below about 2·10⁵, `blocked` is faster: `rare` uses brute force on one core until its mask is
  chosen at n = 32768.
- For `.6` up to 10⁶ (`summary.txt`, `-j 1`): mask `0x1ee`, 1583 rare heap sizes, on average
  about 1580 pairs in step 1 and 2750 in step 2 per heap size, and step 2 never had to scan
  everything.
- Treblecross gains nothing: no mask makes few heap sizes rare.

## Not implemented yet (Grossman §3)

- Generated code for the mex search (the step-1 code is generated, see above; the mex search
  is already short with the speculative mex).

Speculation and check now take similar time on the 24 threads.  Not draining the pipeline at
the mask checkpoints (choosing the mask on the speculative values, which is safe because every
mask is exact) was tried and gained nothing measurable: there are only two checkpoints up to
10⁷, and each drain costs well under 0.1 s.  Pinning threads (`OMP_PROC_BIND=true` with the
team on one CCD) gains about 3%.  Grossman checked the intervals on a cluster, separately from
the speculation, so the paper does not cover this balance.

Step 1 needs only the rare heap sizes and the last 20,628 values, not the whole sequence.  That
is presumably what makes 2⁴⁷ values feasible; the paper does not discuss storage.
