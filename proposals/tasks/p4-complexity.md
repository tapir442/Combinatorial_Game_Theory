# Proposal 4 — Computational complexity of games, sums and poset games: tasks

## Phase 0 — Foundations (months 1–3)

- [ ] **4.1 Background reading** (4 wk). Hearn–Demaine *Games, Puzzles, and
  Computation*, the algorithmic CGT survey by Demaine and Hearn, Fraenkel's complexity
  survey, Nowakowski E1–E3, D1, D2, D4.  *Output:* a literature note.
- [ ] **4.2 Toolkit** (2 wk). Brute-force solvers for switch sums, annihilation games
  and Chomp in Python (`octalgames` style), for experiments.

## Phase 1 — Sums of switches, E3 (months 3–12)

- [ ] **4.3 Experiments** (3 wk). Solve random sums of a‖b|c and a|b‖c with unary
  integers, and look for structure in winning moves.  *Needs:* 4.2
- [ ] **4.4 Algorithm attempt** (4 wk). Thermography or mean/temperature ordering,
  aiming at a pseudo-polynomial algorithm.  *Needs:* 4.3
- [ ] **4.5 Hardness attempt** (6 wk). Reduction from 3-Partition, which is strongly
  NP-hard and so fits unary inputs.  Do 4.4 and 4.5 in parallel and drop the
  loser.  *Needs:* 4.3
- [ ] **4.6 Paper 1** (5 wk). Resolution of E3, or partial results.

## Phase 2 — Closure and annihilation (months 12–24)

- [ ] **4.7 Survey of hard sums** (2 wk). Known NP-hard sums of simple games (Morris,
  Yedwab, Moews) as building blocks for E2.
- [ ] **4.8 Complexity-closure triple, E2** (6 wk). Construct G1, G2, G3, or prove that
  none exists under standard assumptions.  *Needs:* 4.7
- [ ] **4.9 k-annihilation experiments** (3 wk). Outcomes for k = 2 on small digraphs,
  compared with the GF(2) theory for k = 1.  *Needs:* 4.2
- [ ] **4.10 k-annihilation complexity, D4** (6 wk). Find a polynomial algorithm or a
  hardness proof for k ≥ 2.  *Needs:* 4.9

## Phase 3 — Chomp and restricted positions (months 22–36)

- [ ] **4.11 Three-row Chomp data** (3 wk). P-positions up to large n, reproducing the
  Friedman–Landsberg scaling.  Use Brouwer's data where available.
- [ ] **4.12 Unique winning move from 3 × n** (8 wk, risky). Rigorous proof, or a
  partial result for restricted shapes.  *Needs:* 4.11
- [ ] **4.13 Reachable vs all positions** (4 wk). Formalise "positions reachable in
  decent play" (Plambeck).  Check whether the gadgets in the NoGo and Node-Kayles
  hardness proofs are reachable from the empty board.
- [ ] **4.14 Node-Kayles on trees and other classes** (3 wk). Build on the 2025
  arXiv paper and settle one restricted class.

## Phase 4 — Berlekamp's question (months 34–42)

- [ ] **4.15 Natural game for E1** (*open*). Catalogue candidate rulesets against the
  four conditions (simple rules, finitely many options, explicit strategy,
  exponential length) and try to prove NP-hardness for the best candidate.
