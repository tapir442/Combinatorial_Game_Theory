# Proposal 3 — Unified temperature theory for loopy games: tasks

## Phase 0 — Foundations (months 1–4)

- [ ] **3.1 Background reading** (4 wk). Thermography chapters of *WW* and Siegel
  *CGT*, Berlekamp–Wolfe *Mathematical Go*, Spight on dogmatic thermography,
  Nakamura (GONC 3), Nowakowski B1, E4, E5, E20.  *Output:* a literature note.
- [ ] **3.2 CGSuite workflow** (1 wk). Install the CGSuite 2.1.1 in this repo, script
  thermographs and loopy games, and reproduce five textbook thermographs.

## Phase 1 — Short-game foundations (months 4–14)

- [ ] **3.3 Formal proof of Sentestrat** (4 wk). Collect the existing arguments and
  write a rigorous proof (E4, Wolfe).  *Output:* a note or paper.  *Needs:* 3.1
- [ ] **3.4 Top-down thermography** (4 wk). Give an algorithm and conditions under
  which it is efficient (E4).  *Needs:* 3.2
- [ ] **3.5 Dogmatic thermography implementation** (4 wk). Implement it for single
  kos in Python or CGSuite.  *Output:* a tool and test suite.  *Needs:* 3.2
- [ ] **3.6 Monotone Left scaffold, E4.1** (6 wk). Search single-ko positions for
  counterexamples with 3.5, then prove monotonicity.  *Needs:* 3.5
- [ ] **3.7 Paper 1** (5 wk). 3.3, 3.4 and 3.6.

## Phase 2 — Double kos and Go (months 14–28)

- [ ] **3.8 Double-ko game graphs** (3 wk). Formalise several alternating 2-cycles
  joined at one node, with examples from Go literature.
- [ ] **3.9 Dogmatic thermographs of double kos, E4.2** (8 wk). Theory, plus an
  extension of the 3.5 implementation.  *Needs:* 3.6, 3.8
- [ ] **3.10 Jiang–Rui position** (3 wk). Encode the MSRI 2000 corner and settle the
  temperature at N4.  Compare with the Berlekamp–Spight komaster solutions.
  *Needs:* 3.9

## Phase 3 — Atomic weights and unification (months 26–42)

- [ ] **3.11 Atomic weight of stoppers, E5** (6 wk). An algorithm that succeeds
  whenever g lies between loopfree all-smalls of equal atomic weight.
- [ ] **3.12 Atomic weight vs Nakamura liberties, B1** (4 wk). Build a dictionary on
  capturing-race examples, then prove a common formulation or find a difference.
- [ ] **3.13 Boiling-point bounds, E20** (3 wk). Test whether BP ≤ 3k/2 is sharp, and
  look for better general upper bounds.
- [ ] **3.14 Unified loopy temperature, E4.3** (*open*). Propose axioms, check that
  they specialise to Go thermography and to stoppers, and prove them for games
  whose only loops are 2-cycles.
