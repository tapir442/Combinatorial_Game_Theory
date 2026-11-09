# Proposal 1 — Periods and aperiodicity in take-and-break games: tasks

Useful fact: a subtraction game S = {s1, s2, …} is the octal game with digit 3 at each
position s_i.  For example S{1, 2, 4} = `.3303`.  So `octal-explorer` computes
subtraction games without any changes.

## Phase 0 — Foundations (months 1–3)

- [ ] **1.1 Background reading** (3 wk). Start from the chapter *What is known about octal
  games* (`../src/90-background-octal-games.md`).  Then *Winning Ways* ch. 4, Siegel *CGT* ch. IV,
  Nowakowski A1–A3, Ward's conjecture paper, Santos' dynamical-systems bound.
  *Output:* a 10-page literature note.
- [ ] **1.2 Reproduce known results** (1 wk). Use `og.survey(og.octal_codes(2), …)` and
  check every solved two-digit game against Flammenkamp's table.  *Output:* a
  table of agreements, with any discrepancies explained.  *Needs:* 1.1
- [ ] **1.3 Periodicity-theorem checker** (1 wk). Write an independent checker for
  "proved" periods, as a script separate from `octal.c`.  *Output:* a tested script.

## Phase 1 — Three-element subtraction games (months 3–14)

- [ ] **1.4 Period table** (1 wk). Compute preperiod and period of S{a, b, c} for all
  c < 500.  *Output:* a CSV and plots of period against (a, b, c).  *Needs:* 1.2
- [ ] **1.5 Confirm Ward's formula** (1 wk). Check both cases of the conjecture
  against 1.4 and extend the check to c < 4,096.  *Needs:* 1.4
- [ ] **1.6 P-position patterns, case c = a + b** (3 wk). Use the wrap and diff
  pictures (width = p) to describe P-positions within one period, split into the
  regimes 0 ≤ j < a and a ≤ j < 2a.  *Output:* conjectured explicit patterns.
- [ ] **1.7 Prove case 1 for a = 1, 2** (4 wk). Induction on the explicit pattern.
  *Output:* lemma write-up.  *Needs:* 1.6
- [ ] **1.8 Prove case 1 in general** (8 wk, risky). *Needs:* 1.7
- [ ] **1.9 Case 2 divisibility** (10 wk, risky). Prove that p divides a + b, a + c
  or b + c, starting from the data in 1.4.  *Needs:* 1.5
- [ ] **1.10 Purely periodic sets** (3 wk). Characterise the S{a, b, c} with no
  preperiod.  *Needs:* 1.4
- [ ] **1.11 Paper 1** (6 wk). Write up 1.6–1.10.  *Output:* a submission to
  *Integers* or *Electronic J. Combinatorics*.

## Phase 2 — Open octal games (months 12–30)

- [ ] **1.12 Speed up the backend** (4 wk). Parallelise the two-heap scan in
  `octal.c` (OpenMP or bitsets).  Then reimplement Grossman's sparse-space method
  from his GONC 5 paper (`--sparse` currently gives no speed-up).  *Output:* at
  least 10× faster at n = 10⁶.
- [ ] **1.13 Push open three-digit octals** (3 wk compute). Run the A2 list (.14,
  .36, .64, .74, .76, .004 …) to 10⁷–10⁸ and log the growth of the largest value.
  *Needs:* 1.12
- [ ] **1.14 Structure of known periods** (4 wk). Build a database of code, period
  and preperiod, then look for arithmetic features that predict period length.
  *Output:* conjectures.  *Needs:* 1.13
- [ ] **1.15 Splitting families** (3 wk). Add rules for Pulleyblank's "split heaps
  > h" and Singmaster's "both parts ≥ k" to `octal.c`, then compute and look for
  periods.  *Output:* a table and new rule types.

## Phase 3 — .6, .007 and aperiodicity (months 24–42)

- [ ] **1.16 Record statistics for .007** (2 wk). Fit a growth model to the first
  occurrence of each value and predict when 2,048 would appear.  *Needs:* 1.13
- [ ] **1.17 Aperiodicity mechanisms** (3 wk). Study why Fraenkel's subtraction–division
  games and Larsson–Fox are aperiodic, and test whether such a mechanism can occur in
  a finite octal game.  *Output:* a note.
- [ ] **1.18 Structural theorem or first aperiodic octal** (*open*). The headline goal.
  Optionally formalise the periodicity theorem and certificate checker in Lean (6 wk).
