# Proposal 6 — Values, temperatures and nim-dimension of placement games: tasks

## Phase 0 — Foundations (months 1–3)

- [ ] **6.1 Background reading** (3 wk). Berlekamp "Blockbusting and Domineering",
  Uiterwijk's Domineering papers, Santos on nim-dimension, Nowakowski–Santos on
  boiling points.  Plus Nowakowski B11, B12, C1, E6, E17, E19, E20.
- [ ] **6.2 CGSuite baseline** (2 wk). Domineering values of all regions up to 16
  cells in CGSuite, as a reference for 6.3.

## Phase 1 — Solver and database (months 3–14)

- [ ] **6.3 C solver** (6 wk). Canonical forms of Domineering regions with
  decomposition into components, a transposition table and symmetry reduction.
  Possibly a sibling of `octal-explorer` with Python bindings.  Validate against 6.2.
  *Needs:* 6.2
- [ ] **6.4 Region database** (4 wk compute). All regions up to about 30 cells, with
  value, temperature and outcome.  *Output:* a public dataset.  *Needs:* 6.3
- [ ] **6.5 Temperature census** (2 wk). Maximum temperature by region size.  Look
  for positions hotter than 2 or with new temperatures (B11).  *Needs:* 6.4
- [ ] **6.6 Search for ∗4** (3 wk). Search the database, then run a heuristic search
  on larger regions (E17).  *Needs:* 6.4
- [ ] **6.7 Paper 1** (5 wk). Database, census, and new values.

## Phase 2 — Infinite families (months 14–28)

- [ ] **6.8 2 × n values: data** (3 wk). Exact canonical values for even n ≤ 40.
  *Needs:* 6.3
- [ ] **6.9 2 × n values: proof** (6 wk). A closed form that resolves the "ish"
  ambiguity.  *Needs:* 6.8
- [ ] **6.10 3 × n and 4 × n** (4 wk). Outcome patterns, extending Lachmann–Moore–
  Rapaport and Uiterwijk.  *Needs:* 6.3

## Phase 3 — Other rulesets (months 24–36)

- [ ] **6.11 NoGo nim-dimension** (4 wk). One-dimensional boards are linked to .6
  (Officers): compute with `octalgames`, then two-dimensional boards (B12).
- [ ] **6.12 Clobber and Amazons nim-dimension** (6 wk). Constructions or impossibility
  proofs (E17).
- [ ] **6.13 Dots-and-Boxes conjectures** (6 wk). A 1 × n solver for the Faroese and
  Icelandic starts, verified beyond n = 19, then proofs by explicit strategy (C1).

## Phase 4 — Proofs (months 34–42)

- [ ] **6.14 Obstruction techniques, E19** (4 wk). General methods for showing that
  values cannot occur.  Start from the proof that COL has nim-dimension 1.
- [ ] **6.15 Domineering: boiling point 2 and nim-dimension 1** (*open*, headline).
  *Needs:* 6.5, 6.6, 6.14
