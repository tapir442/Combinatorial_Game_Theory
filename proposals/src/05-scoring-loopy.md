# Proposal 5 — Beyond normal play: scoring, loopy and four-outcome games

**Working title:** *Extending Conway's theory: guaranteed scoring games, loopy simplest forms and non-alternating moves*

**Core problem.** Normal-play theory covers only games where the last mover wins and play terminates. Real games such as Go and Dots-and-Boxes are scored, loop, or break move alternation. Several foundational questions about these extensions are open (Problems E5, E11, E14, E23).

**State of the art.**

- Larsson, Nowakowski and Santos built *guaranteed scoring games*, into which normal play order-embeds. They ask whether this is the *largest* subset of scoring games, closed under sum and conjugation, with that property. They also ask for a solution of Brushes (E14).
- Loopy games: the simplest-form theorem holds for *stoppers*, but Bach's Carousel shows that not every loopy game has stoppers. Is there a simplest form for all finite loopy games? (E5)
- Siegel conjectures that every non-alternating Left/Right cycle with at least two moves per player is matched by a single-cycle stopper. He also asks for a *wild* cycle "in nature", in a playable game such as Phutball (E5).
- Four-outcome games (N, P, L, R) that are neither normal nor misère have no general theory (E11).
- Entailing and complimenting moves (Top Entails, Strings-and-Coins): Larsson, Nowakowski and Santos extended impartial theory, and the partizan theory is still in preparation (E23).

**Research questions.**

1. Prove maximality of guaranteed scoring games, or find a larger embedding universe.
2. Solve Brushes, and build a scoring-game library for CGSuite.
3. Define a canonical or simplest form for all finite loopy games, starting with Carousel.
4. Prove Siegel's single-cycle stopper conjecture.
5. Find a wild cycle in a natural game (Phutball, Hare and Hounds, Woodpush).
6. Develop a partizan theory of entailing moves, with Top Entails as the test case.

**Methods.** Partially ordered monoids and Grothendieck-style constructions; game-graph algorithms for loopy games (onside / offside, sidling); and exhaustive search for cycles in rulesets.

**Work plan.**

1. Year 1: guaranteed scoring games; Brushes.
2. Year 2: Siegel's stopper conjecture; Carousel.
3. Year 3: wild cycles in nature; partizan entailing moves.
4. Year 4: four-outcome framework or synthesis; thesis.

**Guaranteed output:** Brushes solved and progress on the scoring-maximality question. **Headline goal:** a simplest-form theory for all finite loopy games.

**Risk.** Medium. The thesis is almost purely theoretical, and suits a student with algebra or order-theory background.
