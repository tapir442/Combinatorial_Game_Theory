# Background — What is known about octal games

This chapter summarises the state of knowledge as of 2026.  It is background mainly for
Proposal 1 (periodicity), and also for Proposal 2 (misère play) and Proposal 4
(complexity).  Problem codes (A2, E16, …) refer to Nowakowski's *Unsolved problems in
combinatorial games* (Games of No Chance 6, 2025).

## Definitions

A take-and-break game is played on heaps of tokens.  Its code $d_0.d_1d_2d_3\ldots$ says
what a move may do: bit $j$ of digit $d_k$ allows removing $k$ tokens from one heap and
leaving exactly $j$ nonempty heaps.  The game is **octal** if every digit is at most 7
(at most 2 heaps left), and **hexadecimal** if digits go up to 15 (up to 3 heaps).  By
Sprague–Grundy theory (1935/1939) every heap of size $n$ is equivalent to a Nim heap
$G(n)$.  A position is a second-player win exactly when the XOR of its heaps' values is
0.  So a game is *solved* once its **nim-sequence** $G(0), G(1), \ldots$ is understood.

## Theory

- **Octal periodicity theorem** (Guy & Smith 1956; *Winning Ways* ch. 4).  Let $t$ be the
  position of the last nonzero digit.  If $G(n+p)=G(n)$ for all $i \le n < 2i+p+t$, then
  the sequence has period $p$ from $i$ on.  So finitely many computed values can *prove*
  periodicity.  This is the basis of every solved octal game.
- **Guy's conjecture** (Problem A2).  Every finite octal game is eventually periodic.  It
  is unproved, and no finite octal game is known to be aperiodic.
- **Subtraction games** (Problem A1).  With a finite subtraction set the nim-sequence is
  always periodic, by a pigeonhole argument.  The period itself is the open part:
    - Ward conjectured a closed formula for three-element sets and checked it for
      $c < 4096$.  It is unproved.
    - Guy conjectures a polynomial bound on the period length, and Santos improved the
      known bound with dynamical-systems methods.
    - Sets with no preperiod are not characterised.
- **Arithmetic periodicity** (Problem A3).  Hexadecimal games, which can split into 3 or
  more heaps, can satisfy $G(n+p)=G(n)+s$ with a *saltus* $s$.  Austin (1976) and
  Howse–Nowakowski give finite tests for this.  Example: `.28` has period 53 and saltus
  16.
- **Infinite codes and variants.**
    - Complements of finite subtraction sets are always arithmetically periodic
      (A. Siegel).  An extreme example is "all except $\{4, 9, 26, 30\}$": preperiod 243,
      period 13,014, saltus 4,702.
    - Moves that depend on $n$ can give aperiodic sequences, as in Fraenkel's
      subtraction–division games and Larsson–Fox.  So aperiodicity is possible *outside*
      finite octal codes.
- **Equivalent codes.**  Different codes can give the same sequence up to a shift.  For
  example `.137` (Dawson's chess) and `.07` (Dawson's Kayles) have the same period 34,
  with preperiods 52 and 53.

## Solved games

| Code | Game | Result | Reference |
|--------|------------------|----------------------------------------------------|------------------------|
| `.77` | Kayles | period 12 from $n = 71$ (last exception $n = 70$) | Guy & Smith 1956 |
| `.07`, `.137` | Dawson's Kayles / chess | period 34 | Guy & Smith 1956 |
| `.127` | | period 4, last exception at $n = 46{,}577$ | Gangolli & Plambeck 1989 |
| `.16`, `.56`, `.376` | | periodic, with long periods | Gangolli & Plambeck 1989 |
| `.106` | | preperiod 465,384,263,797, period 328,226,140,474 | Flammenkamp |

The periods of Kayles, Dawson's Kayles and Dawson's chess were reproduced and proved with
`octal-explorer`.  CGSuite's `CheckPeriodicity` gives the same results.

## Open games

Problem A2 lists **65 open codes**: `.6`, eight two-digit codes (`.04`, `.06`, `.14`,
`.36`, `.37`, `.64`, `.74`, `.76`) and 56 three-digit codes.  The best known are:

- **`.6` Officers.**  No period up to $2^{47}$ (Grossman, GONC 5, with a sparse-space
  method).  Its sequence also appears as one-dimensional NoGo and as Grim on paths.
- **`.007` Treblecross** (one-dimensional tic-tac-toe).  Values are known up to $2^{25}$
  (Flammenkamp), with largest value $G(6{,}193{,}903) = 1401$.  Will 2048 ever be reached?
- **Grundy's game** (split a heap into two unequal heaps).  Values are known up to
  $2^{35}$ with no period (Hoey; Flammenkamp).
- **Couples-are-forever** (split any heap except a heap of 2).  No period in the first
  50 million values.  The families of Pulleyblank (split only heaps $> h$) and Singmaster
  (both parts $\ge k$) are open too.

## Computation and structure

- **Sparse space.**  In many games the values split into *rare* and *common* ones, by the
  parity of $\operatorname{popcount}(v \mathbin{\&} m)$ for a fixed mask $m$.  Flammenkamp
  and Grossman, among others, exploit this to compute far beyond brute force.  (A naive
  rare/common variant in `octal-explorer` gives no speed-up on `.6` or `.007`.  A faithful
  implementation is task 1.12.)
- **Binary block structure.**  `octal-explorer` shows that values just above multiples of
  large powers of two are over-represented.  For `.7213` up to $n = 10^6$, the first four
  values of each block of 256 occur about 6.5× as often as an average value of their
  block.  Treblecross and Officers show the same pattern.  A plausible explanation, not
  proved, is that XOR maps aligned dyadic blocks onto aligned dyadic blocks.  We know of no
  published treatment, which makes this a possible small project.
- **Speed.**  The direct algorithm costs $O(n)$ per heap size, i.e. $O(N^2)$ for $N$
  values.  octal-explorer computes $2\cdot10^5$ values of `.007` in about 6 s.  CGSuite
  2.1.1 is about 13× slower; see `octal-explorer/benchmarks/`.

## Complexity

- How hard is it to compute octal games?  Fraenkel's question is open (Problem E1).
  Note that knowing a period settles a game in constant time per heap.
- On general graphs, **Node Kayles** is PSPACE-complete (Schaefer 1978).  This says
  nothing about a single fixed code on heaps, which corresponds to paths.
- **Lattice games** (Miller–Guo) generalise octal games to several dimensions.  Fink showed
  that some have no efficient strategy, and the squarefree case is conjectured to be
  efficient (Problem E16).

## Misère play

- In misère play the player who makes the last move *loses*, and Sprague–Grundy theory
  breaks down.  Plambeck and Siegel's **misère quotients** (2008) solved many octal games.
  Thousands of quotients are tabulated at miseregames.org.
- **Misère Kayles** is solved (Sibert's analysis, published as Sibert & Conway,
  *Mathematical Kayles*, 1992).
- **Misère Dawson's Kayles** is open.  Its quotient has order 638 at heap size 33, and
  nothing is known from 34 on.  Plambeck offers US\$500 for a solution.  Dawson posed the
  problem in 1935, which may make it the oldest open problem in the field.
- Also open: a precise definition of *algebraic periodicity*, and its detection for `.54`,
  `.261`, `.355`, `.357`, `.516` and `.724` (Problem A15).

## Partizan octal games

Fraenkel and Kotzig introduced partizan octal games, where each player has their own code
(Problem A2).  Mesdal showed that the atomic weights of many all-small cases are
arithmetically periodic.

## References for this chapter

- R. K. Guy, C. A. B. Smith, [The G-values of various games](https://cambridge.org/core/journals/mathematical-proceedings-of-the-cambridge-philosophical-society/article/gvalues-of-various-games/B5C925C1BCF73C0DB7F35EEA160EBB4B),
  *Proc. Cambridge Philos. Soc.* 52 (1956), 514–526.
- A. Gangolli, T. Plambeck, A note on periodicity in some octal games, *Internat. J. Game
  Theory* 18 (1989), 311–320.  The `.127` data are from [OEIS A071464](https://oeis.org/A071464/internal).
- T. J. Schaefer, [On the complexity of some two-person perfect-information games](https://portal.mardi4nfdi.de/wiki/Publication:1248466),
  *J. Comput. System Sci.* 16 (1978), 185–225.
- W. L. Sibert, J. H. Conway, [Mathematical Kayles](https://portal.mardi4nfdi.de/wiki/Item:Q1185758) (1992).
- T. E. Plambeck, A. N. Siegel, [Misère quotients for impartial games](https://arxiv.org/pdf/math/0609825),
  *J. Combin. Theory A* (2008).
- R. J. Nowakowski, [Unsolved problems in combinatorial games](https://library.slmath.org/books/Book71/files/GONC6-04.pdf),
  *Games of No Chance 6*, MSRI Publ. 71 (2025), problems A1–A3, A15, E1, E16.
- E. R. Berlekamp, J. H. Conway, R. K. Guy, *Winning Ways for Your Mathematical Plays*,
  vol. 1, ch. 4.
- A. N. Siegel, *Combinatorial Game Theory*, AMS GSM 146 (2013), ch. IV.
- A. Flammenkamp, [Sprague–Grundy values of octal games](https://wwwhomes.uni-bielefeld.de/achim/octal.html):
  the current status of each code.
