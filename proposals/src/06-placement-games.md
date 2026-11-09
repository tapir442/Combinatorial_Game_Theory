# Proposal 6 — Values, temperatures and nim-dimension of placement games

**Working title:** *What values can Domineering make? Boiling points, nim-dimension and exact values of placement games*

**Core problem.** For a ruleset, the *boiling point* is the supremum of temperatures it attains, and the *nim-dimension* says which nimbers occur (Santos). Both are unknown for most classic games (Problems B11, B12, E6, E17, E20). This proposal combines computation (CGSuite plus custom solvers) with proofs.

**State of the art.**

| Ruleset | Open conjecture or question | Source |
|--------------|--------------------------------------------|----------------|
| Domineering | Boiling point is 2; no position hotter than 2 | E20, B11 (Berlekamp) |
| Domineering | Which nimbers occur? $\ast 2$ and $\ast 3$ are known; whether $\ast 4$ occurs is open | E6, E17; Drummond-Cole, Uiterwijk–Barton |
| Domineering | Exact values of $2 \times n$ (known only up to "ish"), $3 \times n$, $4 \times n$ | B11 |
| Clobber | Nim-dimension 1 | E17 |
| Amazons | Nim-dimension 3 (only $\ast 4$ known) | E17 |
| NoGo | Nim-dimension of 1-D and 2-D boards; bounded denominators on 2-D boards | B12 |
| Nimstring | Infinite nim-dimension (Santos) | E17 |
| Dots-and-Boxes | Faroese $1 \times n$: first player wins by 1 for odd $n \ge 9$ | C1 (Jobson et al., Sierra) |

**Research questions.**

1. Prove that Domineering's boiling point is 2. As a first step, characterise hot Domineering positions up to "ish".
2. Prove that $\ast 4$ cannot occur in Domineering, or construct it.
3. Find exact canonical values of $2 \times n$ Domineering for even $n$, and outcome patterns for $3 \times n$ and $4 \times n$.
4. Determine the nim-dimension of Clobber, Amazons and NoGo. Note that 1-D NoGo contains Officers (.6), which links to Proposal 1.
5. Prove the Faroese and Icelandic $1 \times n$ Dots-and-Boxes conjectures.
6. Find general techniques that show certain values *cannot* occur in a ruleset (E19).

**Methods.** CGSuite for canonical forms; custom alpha-beta and retrograde solvers with transposition tables (following Uiterwijk and Bullock); decomposition into independent regions; temperature bounds via confusion intervals ($\mathrm{BP} \le 3k/2$ when intervals are bounded by $k$); and induction on board width for infinite families.

**Work plan.**

1. Year 1: build a fast decomposition-aware solver integrated with CGSuite; database of all Domineering regions up to about 30 cells.
2. Year 2: $2 \times n$ exact values; search for $\ast 4$ and for hot positions.
3. Year 3: proofs (boiling point, nim-dimension) for Domineering and one other ruleset.
4. Year 4: Dots-and-Boxes conjectures; obstruction techniques; thesis.

**Guaranteed output:** new solved boards, exact values and a public database. **Headline goal:** proofs of the conjectures that Domineering's temperatures never exceed 2 and that $\ast 4$ never occurs.

**Risk.** Low to medium. The computational results publish on their own (ICGA Journal, Integers, Games of No Chance).
