# Proposal 2 — Algebra of misère play: quotients, inverses and cancellation

**Working title:** *Invertibility and cancellation in restricted misère universes*

**Core problem.** In misère play G + (−G) is usually not 0, so misère games form a monoid rather than a group. The theory now works modulo a *universe* (dicots, dead-ending games, impartial positions of one ruleset). Two families of questions are open: the structure of impartial misère quotients (Problems A15, E12) and inverses and cancellation in partizan universes (E13).

**State of the art.**

- Plambeck and Siegel's misère quotients solved many octal games. The quotient of Dawson's Kayles (.07) has order 638 at heap size 33, and its behaviour from heap 34 onward is unknown. Plambeck offers US\$500 for a complete analysis. Dawson posed the problem in 1935, which may make it the oldest open problem in CGT.
- Quotient elements of periods 1, 2, 3, 4, 6 and infinity are known. No period-5 element has been found.
- Fisher et al. and Milley–Renault characterised invertible dicots and invertible dead-ending games. Milley's strip counterexample shows that non-conjugate inverses exist in some universes.
- McKay, Milley, Nowakowski and Santos claim the dead-ending universe is pocancellative (G + X ≥ H + X implies G ≥ H). Davies and Milley (2026) leave it open whether blocking games are.
- Siegel (2023 preprint) built uncountably many dead-ending universes with distinct order relations.

**Research questions.**

1. Is every misère quotient map *faithful*, i.e. do equal quotient images force equal normal-play Grundy values? (E12.1)
2. Exhibit a quotient with a period-5 (or period-8) element, or prove that periods are restricted. Find the smallest quotient with a period-3, 4 or 6 element. (E12.4–5)
3. Extend the classification of quotients beyond order 18. (E12.3)
4. In which universes are there no non-conjugate inverses? Test the conjecture that parental, dense and reverse-friendly universes have none. (E13.2)
5. Are the dicot, dead-ending and full misère universes cancellative? (E13)
6. Give a precise definition of *algebraic periodicity* and an algorithm to detect it. Apply it to .54, .261, .355, .357, .516 and .724.

**Methods.** Finitely generated commutative monoids and Rédei's theorem; semigroup theory (Green's relations, maximal subgroups); computer algebra (GAP, Plambeck and Siegel's MisereSolver, CGSuite's misère engine); and exhaustive enumeration of small quotients.

**Work plan.**

1. Year 1: master the quotient machinery; reproduce known quotients; push the classification of small quotients past order 18.
2. Year 2: targeted search for period-5 elements; faithfulness question; first results on cancellation in dicots.
3. Year 3: inverses in general universes; a definition and detection algorithm for algebraic periodicity.
4. Year 4: attack Dawson's Kayles heap 34+ with the new tools; thesis.

**Guaranteed output:** an enlarged classification of quotients plus answers to at least one E12 question. **Headline goal:** a structure theorem for Dawson's Kayles, or a general algorithm for algebraically periodic misère octals.

**Risk.** Low to medium. The questions are numerous and largely independent, and the community is active (Dalhousie, Mount Allison, Lisbon, Kyoto).
