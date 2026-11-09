# Proposal 4 — Computational complexity of games, sums and poset games

**Working title:** *Where tractability ends: complexity of game sums, poset games and simple rulesets*

**Core problem.** Many natural games are PSPACE-complete, but the boundary between easy and hard is poorly mapped for disjunctive *sums* and for poset games. Berlekamp and Fraenkel also ask for a game that is hard to *play* yet has a simple explicit strategy (Problem E1).

**State of the art.**

- Fraenkel reformulates Berlekamp's question (E1). Is there a game with simple playable rules, finitely many options per move, a simple explicit strategy and at most exponential length that is still NP-hard? Nimania and Multivision come close but fail one condition.
- Fraenkel asks for *complexity closure* (E2): partizan $G_1, G_2, G_3$ where every pair sum except $G_1 + G_3$ is polynomial, but $G_1 + G_3$ is NP-hard.
- Wolfe asks whether winning a sum of switches $a \parallel b \mid c$ or $a \mid b \parallel c$, with unary integers, is in P or NP-hard (E3).
- Fraenkel and Yesha give an $O(n^6)$ algorithm for 1-annihilation games. The complexity for $k \gt 1$ is open (D4).
- Chomp: 2-row poset games are eventually periodic (Byrnes). Three-rowed Chomp looks chaotic (Friedman–Landsberg renormalisation), and no rigorous proof of a unique winning move from $3 \times n$ exists. Gale's US\$300 3-D infinite Chomp is open (D1).
- Brouwer and Christensen (2018) disproved the Gale–Neyman conjectures for Subset Take-Away (D2), leaving the true pattern open.
- Plambeck asks whether complexity changes when positions are restricted to those reachable in decent play.

**Research questions.**

1. Settle Wolfe's switch-sum problem (E3), either with a pseudo-polynomial algorithm via thermography or with a reduction from Partition or 3-SAT.
2. Construct a complexity-closure triple (E2), or prove that none exists under standard assumptions.
3. Find the complexity of $k$-annihilation for $k \ge 2$ (D4).
4. Give a rigorous analysis of 3-rowed Chomp: existence and uniqueness of the winning move from $3 \times n$, and the scaling laws.
5. Answer Berlekamp's E1 question with a natural game.
6. Compare the complexity of "all positions" and "reachable positions" for Node-Kayles, NoGo and Domineering.

**Methods.** Reductions (constraint logic, Hearn and Demaine), parameterised complexity, algorithmic thermography, linear algebra over GF(2) for annihilation games, and computational exploration of Chomp (Brouwer's data).

**Work plan.**

1. Year 1: survey; E3 hardness or algorithm.
2. Year 2: E2 construction; $k$-annihilation.
3. Year 3: 3-rowed Chomp; the reachable-positions question.
4. Year 4: E1; thesis.

**Guaranteed output:** at least one of E3 or D4 resolved, plus new hardness results for natural rulesets. **Headline goal:** a natural game answering Berlekamp's question, or a rigorous theory of 3-rowed Chomp.

**Risk.** Medium to high. Complexity questions can stall, so the plan mixes several independent targets.
