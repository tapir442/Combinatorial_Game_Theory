# Proposal 3 — Towards a unified temperature theory for loopy games

**Working title:** *Thermography beyond short games: kos, loops and atomic weights*

**Core problem.** Siegel calls a temperature theory that covers *all* loopy games "among the most important open problems in combinatorial game theory" (Problem E4.3). Go's ko-based temperature theory and the classical loopy theory, where infinite play is a draw, look radically different. A "grand unified temperature theory" would reconcile them.

**State of the art.**

- Hanner and Milnor founded thermography. Berlekamp and Spight developed komaster and *dogmatic* (neutral ko-threat environment) thermography for Go.
- There is no machinery for dogmatic thermographs of double kos (several alternating 2-cycles joined at one node). Siegel names this as the obvious next step (E4.2).
- It is unproved that the Left scaffold of a dogmatic thermograph decreases in $t$ (E4.1). The statement is false for komaster thermographs.
- Wolfe asks for a formal proof that Sentestrat works and for an efficient top-down thermography algorithm (E4).
- Atomic weight is well defined only for some loopy infinitesimals. Siegel asks for an algorithm that computes it whenever $g$ sits between loopfree all-smalls of equal atomic weight (E5).
- Berlekamp asks for a common framework for atomic weights and Nakamura's liberty-counting theory of Go capturing races, or for proof that they differ (B1).

**Research questions.**

1. Prove monotonicity of dogmatic Left scaffolds (E4.1).
2. Build the theory and an algorithm for dogmatic thermographs of double kos (E4.2). Validate them on Berlekamp's Jiang–Rui MSRI 2000 position, where the temperature at N4 is still unsettled.
3. Propose axioms for temperature on general loopy games that specialise to both Go thermography and classical stoppers (E4.3).
4. Give an algorithm for the atomic weight of infinitesimal stoppers (E5).
5. Unify atomic weight with Nakamura's liberties, and extend Nakamura's theory to kos (B1).
6. Bound *boiling points*: show that confusion intervals bounded by $k$ give $\mathrm{BP} \le 3k/2$ sharply, and find better general upper bounds (E20).

**Methods.** Thermographic calculus; ordered abelian groups and the loopy simplest-form theorem; an implementation in CGSuite (its loopy-game and thermograph modules); and Go endgame databases for test cases.

**Work plan.**

1. Year 1: formal proof of Sentestrat; monotonicity of dogmatic scaffolds.
2. Year 2: double-ko thermographs, implemented in CGSuite; the Jiang–Rui position.
3. Year 3: atomic weights of stoppers; the atomic weight–liberties link.
4. Year 4: axioms for unified loopy temperature; thesis.

**Guaranteed output:** E4.1 and Sentestrat proofs plus working software. **Headline goal:** a first coherent unified loopy temperature theory.

**Risk.** Medium. Year 1–2 results are concrete, and the open-ended unification can stop at a well-defined subclass (for example games whose only loops are 2-cycles).
