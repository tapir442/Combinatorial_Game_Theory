# Proposal 2 — Algebra of misère play: tasks

## Phase 0 — Foundations (months 1–4)

- [ ] **2.1 Background reading** (3 wk). Plambeck–Siegel on misère quotients, Siegel
  *CGT* ch. V, Milley–Renault survey (GONC 5), Nowakowski A15, E12, E13.
  *Output:* a literature note.
- [ ] **2.2 Quotient engine** (5 wk). Get Siegel's MisereSolver or write a quotient
  calculator for octal games.  It could be a module of `octalgames` that reuses the C
  backend for normal-play values.  *Output:* a tested tool.
- [ ] **2.3 Reproduce known quotients** (2 wk). Dawson's Kayles (.07) up to heap 33
  (order 638), plus 10 games from miseregames.org.  *Needs:* 2.2

## Phase 1 — Impartial quotients, E12 (months 4–20)

- [ ] **2.4 Enumerate small quotients** (4 wk). Enumerate commutative monoids with a
  P-subset that satisfy the quotient axioms, up to order 18, with GAP/smallsemi or
  custom code.  Compare with the published counts.  *Output:* a database.
- [ ] **2.5 Beyond order 18** (6 wk). Symmetry breaking and SAT/constraint solving
  (E12.3).  *Needs:* 2.4
- [ ] **2.6 Smallest quotients with periods 3, 4, 6** (2 wk). Read them off the
  database (E12.5).  *Needs:* 2.4
- [ ] **2.7 Faithfulness** (3 wk). Test every computed quotient map for a
  counterexample, then attempt a proof (E12.1).  *Needs:* 2.3
- [ ] **2.8 Maximal subgroups** (3 wk). Does every maximal subgroup S meet P (E12.2)?
  Check computationally, then attempt a proof.  *Needs:* 2.4
- [ ] **2.9 Period-5 element** (6 wk, risky). Search realisable quotients, or prove
  an obstruction (E12.4).  *Needs:* 2.5
- [ ] **2.10 Paper 1** (5 wk). Classification plus 2.6–2.8.

## Phase 2 — Partizan universes, E13 (months 18–32)

- [ ] **2.11 Partizan comparison tools** (3 wk). Implement comparison modulo the dicot
  and dead-ending universes, possibly via CGSuite scripts, and test on games born by
  day 2 (Dwyer–Milley–Willette).
- [ ] **2.12 Non-conjugate inverses** (4 wk). Search small games in parental, dense
  and reverse-friendly universes, then attempt the conjectured theorem.
  *Needs:* 2.11
- [ ] **2.13 Cancellativity of dicots** (8 wk, risky). Does G + X = H + X imply
  G = H?  Start from Siegel's impartial proof.  *Needs:* 2.11
- [ ] **2.14 Pocancellativity of blocking games** (4 wk). Davies–Milley's open
  question.  *Needs:* 2.11

## Phase 3 — Algebraic periodicity and Dawson's Kayles (months 30–42)

- [ ] **2.15 Algebraic periodicity** (8 wk). Write a precise definition and a
  detection algorithm.  Test them on .54, .261, .355, .357, .516 and .724.
  *Needs:* 2.3
- [ ] **2.16 Dawson's Kayles from heap 34** (*open*). Presentations of D34, D35, …
  using 2.15.  The prize problem.
