# Proposal 1 — Periods and aperiodicity in take-and-break games

**Working title:** *The structure of nim-sequences: from three-element subtraction games to Treblecross*

**Core problem.** Guy conjectured that every finite octal game has an eventually periodic nim-sequence (Nowakowski, Problem A2). No finite octal game has been shown to be aperiodic. Dozens of small cases are still open, among them .6 (Officers), .007 (Treblecross), .14, .36, .64 and Grundy's game.

**State of the art (2025).**

- Flammenkamp settled .106. Its preperiod is 465,384,263,797 and its period is 328,226,140,474, so periods can be astronomically long.
- Treblecross (.007) has been computed to heap size $2^{25}$ with no period. Its largest nim-value so far is G(6,193,903) = 1,401. Nowakowski asks: will 2,048 ever be reached?
- Grossman's sparse-space method took Officers (.6) to heap size $2^{47}$ with no period.
- Conway's Couples-Are-Forever shows no period in its first 50 million values.
- For subtraction games S = {a, b, c}, Ward verified a conjectured closed formula for the period for all c < 4,096, but it is unproved (Problem A1).

Ward's conjecture, case c = a + b, with j ≡ b − a (mod 2a) and 0 ≤ j < 2a:

$$
p = \begin{cases} 2b + a - j, & 0 \le j < a, \\[2pt] a(2b - a + j)/\gcd(a,\, 2a - j), & a \le j < 2a. \end{cases}
$$

If c ≠ a + b, the conjecture says p divides a + b, a + c or b + c, and equals the gcd of those sums that it divides.

**Research questions.**

1. Prove Ward's period formula for three-element subtraction games. Then bound period length in terms of the largest element (Guy: a polynomial of degree at most n²).
2. Explain the period structure of known periodic octal games. Which arithmetic features of the code predict the period length?
3. Push .6, .007 and Grundy's game further with new sparse-space and parallel methods. Then either find periods or gather evidence for aperiodicity.
4. Find the first provably aperiodic finite octal game, or prove a structural theorem that rules it out for a natural subclass.

**Methods.** Combinatorics on words and automatic sequences; symbolic dynamics (following Santos' dynamical-systems bound); large-scale computation (bit-parallel mex, GPU sparse-space search, CGSuite for small cases); and formal verification (Lean or Coq) of periodicity certificates.

**Work plan.**

1. Year 1: survey; re-implement Flammenkamp and Grossman's algorithms; complete proof of Ward's conjecture for case 1 (c = a + b).
2. Year 2: case 2 of Ward; period-length bounds; periodicity proofs for 2–3 open three-digit octals.
3. Year 3: large computations on .6 and .007; Couples-Are-Forever and Pulleyblank / Singmaster splitting families.
4. Year 4 (optional): aperiodicity theory; thesis.

**Guaranteed output:** a proof of Ward's formula or a large subfamily of it, plus new periodicity certificates. **Headline goal:** the first aperiodic octal game, or a period for .6 or .007.

**Risk.** The headline goal may resist all attacks, as it has since the 1950s. The subtraction-game part is self-contained and publishable either way.
