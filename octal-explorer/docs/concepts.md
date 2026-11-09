# Concepts

## Take-and-break games and nim-values

A position is a collection of heaps of beans.  A move picks one heap, removes some
beans from it, and may split what is left into several heaps.  The player who cannot
move loses (normal play).

By the Sprague–Grundy theorem, each heap of size \(n\) is equivalent to a Nim heap of
size \(G(n)\), its **nim-value**:

\[
G(n) = \operatorname{mex}\{\, G(a_1) \oplus \dots \oplus G(a_j) : (a_1,\dots,a_j) \text{ reachable from } n \,\}
\]

Here mex is the smallest non-negative integer not in the set, and \(\oplus\) is XOR.  A
sum of heaps is a second-player win (a P-position) exactly when the XOR of their
nim-values is 0.  The sequence \(G(0), G(1), \dots\) is the **nim-sequence**.

## Game codes

| Code | Meaning |
|---|---|
| `.d1d2d3…` | Digit \(d_k\) (hexadecimal) says what may happen when \(k\) beans are removed |
| bit 0 (1) of \(d_k\) | allowed to leave **no** heap (remove a whole heap of size \(k\)) |
| bit 1 (2) | allowed to leave **one** nonempty heap |
| bit 2 (4) | allowed to leave **two** nonempty heaps |
| bit 3 (8) | allowed to leave **three** nonempty heaps (hexadecimal games) |
| `d0.…` | digit before the point: split without removing (bits 2 and 3 only) |
| `grundy` | Grundy's game: split one heap into two **unequal** heaps |
| `couples` | Couples-are-forever: split any heap into two, except a heap of 2 |

Examples:

| Code | Game | Status |
|---|---|---|
| `.77` | Kayles | period 12, preperiod 71 |
| `.07` | Dawson's Kayles | period 34, preperiod 53 |
| `.137` | Dawson's chess | period 34 |
| `.6` | Officers | **open** |
| `.007` | Treblecross (one-dimensional tic-tac-toe) | **open** |
| `.3303` | subtraction game S{1, 2, 4} | period 3 |
| `4.` | split any heap into two | |

!!! tip "Subtraction games"
    The subtraction game with set \(S\) is the octal game with digit 3 at every position
    \(s \in S\) and 0 elsewhere.  For example \(S = \{1, 3, 4\}\) is `.3033`.

## Periods: proved and apparent

A sequence has **period \(p\) and preperiod \(i\)** if \(G(n+p) = G(n)\) for all
\(n \ge i\).

The **octal periodicity theorem** (Guy and Smith; *Winning Ways*, ch. 4): let \(t\) be
the position of the last nonzero digit.  If

\[
G(n + p) = G(n) \quad\text{for all } i \le n < 2i + p + t,
\]

then the sequence has period \(p\) from \(i\) on, forever.  So finitely many values
prove periodicity.

octal-explorer reports:

- **proved** when the computed range contains enough values for the theorem
  (\(N \ge 2i + 2p + t\)).  This is claimed only for plain octal codes: no digit before
  the point, no hexadecimal digits ≥ 8, no named rules.
- **apparent** when the smallest \(p\) repeats at least three times at the end of the
  data (\(N - i \ge 3p\)) but the theorem does not apply or needs more values.
- **none** otherwise.

### Arithmetic periodicity (saltus)

Hexadecimal games can split a heap into three, and their values can grow without
bound.  The right notion is then **arithmetic periodicity**: a preperiod \(i\), a period
\(p\) and a **saltus** \(s\) with

\[
G(n + p) = G(n) + s \quad\text{for all } n \ge i.
\]

Ordinary periodicity is \(s = 0\).  octal-explorer also looks for these.  For each
candidate \(p\) it takes \(s = G(N-1) - G(N-1-p)\) from the end of the data and finds
the earliest \(i\) from which the relation holds.

- Arithmetic periods are **proved** with Austin's Theorem 6.8 when its hypotheses hold
  (see below), and otherwise reported as **apparent**.
- To avoid chance matches, an arithmetic period must hold for at least three periods
  **and** at least a quarter of the computed values.

| Game | Result |
|---|---|
| `.28`, `.29` | period 53, saltus 16, preperiod 1, **proved** (Austin 6.8); matches Nowakowski, A3 |
| `.9c` | period 36, saltus 16, preperiod 28, **proved**; matches A3 |
| `.b8` | period 7, saltus 4, preperiod 9, **proved** from 66 values, Austin's own example |
| `.f8` | period 6, saltus 4, preperiod 7, **proved** (Austin used 12/8, see below) |
| `.3f` | period 6, saltus 3: apparent only, since 3 is not a power of 2 |
| `.f6` | period 43, saltus 32, preperiod 604 up to n ≈ 1900, unproved; **none** from n ≈ 1930 |

### Austin's Theorem 6.8

R. B. Austin, *Impartial and partisan games*, MSc thesis, University of Calgary (1976),
Theorem 6.8.  Let \(d_0.d_1\ldots d_t\) (\(t \ge 1\)) be a hexadecimal code, so a move leaves at
most three heaps.  Let \(e\) be the last irregular index, \(p \ge t+2\) and \(s = 2^k \ge 1\).
Suppose:

1. \(G(i+p) = G(i) + s\) for all \(e < i < e + 7p + t\);
2. \(G(i) < s\) for all \(i \le e\);
3. \(G(i) < 2s\) for all \(i \le e + p\);
4. either
    - some odd-indexed digit \(d_{2v+1}\) and some even-indexed digit \(d_{2w}\) both
      contain 8, and every \(g < 2s\) occurs as some \(G(i)\) with \(i > 0\); or
    - some digit \(d_u\) contains 8, and every \(g < 2s\) occurs both as \(G\) of an odd
      heap and as \(G\) of an even heap.

  ("\(d\) contains 8" means 8 occurs in the binary expansion of \(d\): a move may leave
  three heaps.)

Then \(G(i+p) = G(i) + s\) for **all** \(i > e\).

octal-explorer uses the smallest possible \(e\) (preperiod − 1).  Conditions 2–3, the
power of 2 and \(p \ge t+2\) may hold only for a multiple \(mp, ms\), so \(m = 1, 2, \ldots\)
are tried.  The certified multiple is reported as `proof_period` / `proof_saltus`.

For `.F8` Austin writes that he had to use period 12 and saltus 8.  His sequence
notation starts the period after \(G(9) = 4\), i.e. \(e = 9\), where condition 2 fails
for \(s = 4\).  With the smallest \(e = 6\) all hypotheses already hold for period 6 and
saltus 4.  The generalisation by Howse and Nowakowski (2004) is not implemented.

In `diff` pictures, use the saltus: cells are dark where \(G(n) - G(n-W) \ne s\).  When
the width is the detected period, the detected saltus is used automatically, so
arithmetic periods also show up as blank areas.

!!! warning
    "No period found" only means that none is visible within the computed range.
    Some solved games have enormous periods: `.106` has preperiod 465,384,263,797 and
    period 328,226,140,474.

## Reading the pictures

| Picture | What it shows | What to look for |
|---|---|---|
| **values** | \(G(n)\) against \(n\); new record values in red | bounded values suggest periodicity; growing bands (as in `.007`) are structure in an aperiodic-looking game |
| **wrap** | rows of \(W\) consecutive values; colour = value, 0 near-black | when \(W\) is a period or a multiple of one, the picture becomes **vertical stripes** |
| **diff** | dark where \(G(n) \ne G(n-W)\) | when \(W\) is a period, the picture is **blank below the preperiod**; the last dark cell is the last exception |
| **autocorrelation** | for lag \(p\), the fraction of \(n\) in the second half with \(G(n) = G(n+p)\) | peaks at candidate periods; near-misses show "almost periods" |
| **histogram** | how often each value occurs (log scale) | rare values; the gaps used by the rare/common-value method |
| **folded frequencies** (`plot_folded`) | frequency of each value relative to the average of its block of \(B\) values, folded modulo \(B\) (profile), and per block (heat map) | peaks at \(v \equiv 0 \pmod B\): values just above multiples of large powers of two are favoured (see below) |
| **bit planes** (explorer) | bit \(b\) of \(G(n)\) | regularities in individual bits often appear before full periodicity |

!!! note "Why values just above multiples of powers of two are frequent"
    In `.7213`, `.007` and `.6` the values \(v = k\cdot 256 + r\) with small
    \(r\) occur several times more often than average.  For `.7213` up to
    \(n = 10^6\), the first four values of each block of 256 occur about 6.5× as
    often as an average value of their block, and the last quarter only 0.4×.  A
    plausible explanation (not proved): XOR keeps aligned dyadic blocks together.
    Values from \([a2^j,(a+1)2^j)\) and \([b2^j,(b+1)2^j)\) XOR to exactly the
    block \([(a\oplus b)2^j,\dots)\), so the set of option values tends to be a union
    of whole aligned blocks.  The mex, the first gap, then lies at the start of a
    block.

The colour of value \(v > 0\) has hue \(v \cdot 0.618034 \bmod 1\) (the golden-ratio
step), saturation 0.70 and value 0.92.  Value 0 is \((25, 25, 35)\).  The C program, the
HTML explorer and the Python package all use the same palette.

## Algorithm and cost

The nim-value of \(n\) is a mex over all options.  Two-heap moves give about \(n/2\)
options per removal, so computing \(N\) values costs \(O(N^2)\).  On one core that is
roughly 6 s for \(N = 200{,}000\) of `.6` or `.007`.  Hexadecimal (three-heap) moves cost
\(O(n^2)\) per heap size.

The command-line program also has an experimental `--sparse` mode: a rare/common-value
split by the parity of `popcount(v & mask)`.  It is verified against brute force but is
not faster on the games tested.  See [Internals](c-internals.md#the-sparse-mode).
