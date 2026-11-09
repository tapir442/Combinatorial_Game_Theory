# Proposal 5 — Beyond normal play: scoring, loopy and four-outcome games: tasks

## Phase 0 — Foundations (months 1–4)

- [ ] **5.1 Background reading** (4 wk). The Larsson–Nowakowski–Santos papers on
  guaranteed scoring, the scoring survey, the loopy chapters of *WW* and Siegel *CGT*,
  and the entailing-moves paper.  Plus Nowakowski E5, E11, E14, E23.
  *Output:* a literature note.
- [ ] **5.2 Scoring-game library** (4 wk). Guaranteed scoring games in Python or
  CGSuite: sums, conjugates, comparison, canonical forms.  *Output:* a tested library.

## Phase 1 — Guaranteed scoring games, E14 (months 4–16)

- [ ] **5.3 Brushes: data** (3 wk). Values of all small Brushes positions.
  *Needs:* 5.2
- [ ] **5.4 Brushes: solution** (5 wk). Conjecture and prove a formula.
  *Needs:* 5.3
- [ ] **5.5 Maximality question** (6 wk). Is the guaranteed class the largest
  subset, closed under sum and conjugation, into which normal play order-embeds?
  Characterise the obstructions, such as ⟨∅ℓ | ∅r⟩ with ℓ > r.  *Needs:* 5.2
- [ ] **5.6 Paper 1** (5 wk). 5.4 and 5.5.

## Phase 2 — Loopy games, E5 (months 14–30)

- [ ] **5.7 Loopy toolkit** (3 wk). Game-graph sidling (onside/offside)
  implementation, cross-checked against CGSuite.
- [ ] **5.8 Carousel** (4 wk). A full analysis of Bach's Carousel and a candidate
  notion of simplest form.  *Needs:* 5.7
- [ ] **5.9 Single-cycle stoppers** (6 wk). Generate cycles Q (non-alternating, at
  least two moves per player) and construct matching stoppers by computer, then prove
  Siegel's conjecture.  *Needs:* 5.7
- [ ] **5.10 Wild cycles in nature** (4 wk). A cycle detector for small boards of
  Phutball, Hare and Hounds and Woodpush, to find a wild cycle in a playable game.
  *Needs:* 5.7
- [ ] **5.11 Simplest form for all finite loopy games** (*open*, headline).
  *Needs:* 5.8, 5.9

## Phase 3 — Non-alternating and four-outcome games (months 28–42)

- [ ] **5.12 Partizan entailing moves, E23** (8 wk). Extend the impartial theory, with
  Top Entails as the test case.
- [ ] **5.13 Natural games with entailing moves** (3 wk). Search for rulesets with a
  complete solution, as Conway asked.
- [ ] **5.14 Four-outcome games, E11** (*open*). Guy's parity subtraction game as the
  model.  Build a framework, or prove that no normal- or misère-style theory exists.
