# Rare-value algorithm (Grossman)

`--rare` (C: `octal_extend_rare`, Python: `method="rare"`) implements the *rare-value
algorithm* as described by J.P. Grossman, **Searching for periodicity in Officers**, *Games of
No Chance 5*, MSRI Publications 70 (2017), 373–385
([PDF](https://library.slmath.org/books/Book70/files/1016.pdf); a copy is in
`literature/Grossman-2017-Searching-for-periodicity-in-Officers.pdf`).  Grossman credits the
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
  step, how often step 2 had to scan everything, and the speculation statistics.

## Speculation (Grossman §3.1)

Step 2 is about 60% of the work, and it almost never changes the result.  So `rare_spec_chunk`
works on chunks of \(n/8\) heap sizes (between 4096 and 2¹⁸, ending at the next mask checkpoint):

1. **Speculate** on one core: compute every value of the chunk with step 1 only, that is,
   assume that no new rare value appears.  Speculative values are always common.
2. **Check** in parallel: recompute every value of the chunk with the full algorithm, taking
   the speculative values before it as given.  Given those values, the heap sizes are
   independent, so the check runs on all threads (OpenMP), each with its own mark table.
3. If every value agrees, all are correct, by induction over \(n\).  Otherwise let \(f\) be
   the first heap size that disagrees.  The values before \(f\) are correct, so the value the
   check computed for \(f\) is correct too: it is a new rare value.  It is stored and added to
   the rare list, and speculation resumes at \(f + 1\); the speculative values after \(f\)
   are thrown away.

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

So the rare-value algorithm does **not** help Treblecross: about 10% of its heap sizes are
rare under the best mask, and new ones keep appearing.  Flammenkamp's computation of `.007`
to 2²⁵ must rely on something else.

## Benchmark

`benchmarks/bench_methods.py` (results in `benchmarks/results/methods.md`) times every method
through the command-line program and checks that all give identical values.

Wall-clock seconds on a Ryzen 9 7900X (12 cores, 24 threads; GPU = its integrated Radeon).
`brute` and `rare -j 1` use one core; `blocked` and `rare` (speculation + parallel check) use
all 24 threads.

| code | n | brute | blocked | gpu (Vulkan) | rocm | rare -j 1 | **rare** |
|---|---:|---:|---:|---:|---:|---:|---:|
| `.6` | 10⁵ | 1.55 | 0.12 | 0.23 | 0.43 | 0.60 | 0.35 |
| `.6` | 3·10⁵ | 13.1 | 0.90 | 2.00 | 2.14 | 1.80 | **0.59** |
| `.6` | 10⁶ | – | 9.53 | 22.7 | 19.9 | 6.43 | **1.80** |
| `.6` | 10⁷ | – | – | – | – | 60.9 | **15.2** |
| `grundy` | 10⁶ | – | 8.91 | 22.4 | 20.4 | 5.38 | **1.41** |
| `grundy` | 10⁷ | – | – | – | – | 54.0 | **13.3** |
| `couples` | 10⁶ | – | 9.57 | 22.7 | 20.8 | 5.57 | **1.64** |
| `couples` | 10⁷ | – | – | – | – | 56.1 | **14.1** |
| `.007` | 3·10⁵ | 12.8 | 1.12 | 2.14 | 2.27 | 13.0 | 12.9 (falls back to brute) |
| `.007` | 10⁶ | – | 9.88 | 23.2 | 19.8 | – | – |

The full table, with all sizes, is in `benchmarks/results/methods.md`.

- For the three sparse games the plain rare-value algorithm runs at an almost constant
  **≈ 170,000 values per second** on one core (10⁶ → 10⁷ takes 10× as long, not 100×).  Grossman
  reports 88,000 values/s for the same algorithm on a 2008 Xeon.
- **Speculation** makes it 3.6–4.1× faster again: **≈ 660,000–750,000 values per second**.
  Grossman's speculation gave 2.2× (196,000 values/s); here the check also runs on all cores.
- At \(n = 10^6\), `rare` is 5–6× faster than `blocked` on all 24 threads, and about 80× faster
  than brute force (143 s for `.6`).  At \(10^7\) the O(n²) methods would need roughly 100× their
  10⁶ time (about 1000 s for `blocked`), so `rare` is then about 60× faster than `blocked`, and
  the gap keeps growing with \(n\).
- Below about 2·10⁵, `blocked` is faster: `rare` uses brute force on one core until its mask is
  chosen at n = 32768.
- For `.6` up to 10⁶ (`summary.txt`, `-j 1`): mask `0x1ee`, 1583 rare heap sizes, on average
  about 1580 pairs in step 1 and 2750 in step 2 per heap size, and step 2 never had to scan
  everything.
- Treblecross gains nothing: no mask makes few heap sizes rare.

## Not implemented yet (Grossman §3)

Grossman's further speed-ups, about 60× in total together with speculation:

- **Overlapping speculation and check.**  Here the next chunk is speculated only after the
  check of the current one; running both at the same time would hide most of the check.
- **One byte per value.**  All Officers values are below 512, and for a common value bit 8
  follows from the others, so values fit in a byte and the mark array has 256 entries.
- **Generated, fully unrolled code** for step 1 and for the mex search: the rare heap sizes
  and their values are constants, about three instructions per rare heap.
- **Baton passing.**  Thread \(j\) computes the \(n \equiv j \pmod{m}\), marks everything that
  does not depend on the last \(m - 1\) values in advance, receives those values from the
  previous thread, finishes, and passes them on.  Plus a speculative mex before the baton
  arrives.

Step 1 needs only the rare heap sizes and the last 20,628 values, not the whole sequence.  That
is presumably what makes 2⁴⁷ values feasible; the paper does not discuss storage.
