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
- then runs `grundy_rare` (steps 1 and 2 above), appending each new rare heap size;
- reports in `summary.txt` the mask, the number of rare heap sizes, the pairs examined in each
  step and how often step 2 had to scan everything.

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
`brute` and `rare` use one core, `blocked` uses all 24 threads.

| code | n | brute | blocked | gpu (Vulkan) | rocm | **rare** |
|---|---:|---:|---:|---:|---:|---:|
| `.6` | 10⁵ | 1.52 | 0.12 | 0.42 | 0.54 | 0.60 |
| `.6` | 3·10⁵ | 13.1 | 1.13 | 2.04 | 2.53 | 1.74 |
| `.6` | 10⁶ | – | 9.76 | 22.8 | 19.4 | **6.10** |
| `.6` | 10⁷ | – | – | – | – | **57.9** |
| `grundy` | 10⁶ | – | 8.95 | 22.4 | 19.2 | **5.13** |
| `grundy` | 10⁷ | – | – | – | – | **53.5** |
| `couples` | 10⁶ | – | 9.95 | 23.4 | 27.7 | **6.15** |
| `couples` | 10⁷ | – | – | – | – | **58.1** |
| `.007` | 3·10⁵ | 13.4 | 1.13 | 2.02 | 2.25 | 13.6 (falls back to brute) |
| `.007` | 10⁶ | – | 9.71 | 23.4 | 20.9 | – |

The full table, with all sizes, is in `benchmarks/results/methods.md`.

- For the three sparse games the rare-value algorithm runs at an almost constant
  **≈ 170,000 values per second** on one core (10⁶ → 10⁷ takes 10× as long, not 100×).  Grossman
  reports 88,000 values/s for the same algorithm on a 2008 Xeon.
- At \(n = 10^6\) one core of `rare` beats all 24 threads of `blocked` by 1.6×, and brute force on
  one core by about 23× (brute force for `.6` to 10⁶ takes 143 s).  At \(10^7\) the O(n²)
  methods would need roughly 100× their 10⁶ time (about 1000 s for `blocked`), so `rare` is
  then about 17× faster; and the gap keeps growing with \(n\).
- Below about 3·10⁵, `blocked` is faster: `rare` uses brute force until its mask is chosen at
  n = 32768, and one core is slower than 24 for that part.
- For `.6` up to 10⁶ (`summary.txt`): mask `0x1ee`, 1583 rare heap sizes, on average about
  1580 pairs in step 1 and 2750 in step 2 per heap size, and step 2 never had to scan
  everything.  Step 2 is 63% of the work, which is what Grossman's speculation removes.
- Treblecross gains nothing: no mask makes few heap sizes rare.

## Not implemented yet (Grossman §3)

Grossman's speed-ups beyond the basic algorithm, about 60× in total:

- **Speculation.**  Run only step 1, assuming no new rare values, and verify intervals of the
  result in parallel (on a cluster) with the full algorithm; induction over the intervals
  makes the whole sequence correct.
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
