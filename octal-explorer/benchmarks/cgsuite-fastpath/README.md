# CGSuite: fast path for octal and hexadecimal nim-sequences

Status: **notes for a future upstream issue/PR** at
[aaron-siegel/cgsuite](https://github.com/aaron-siegel/cgsuite).  Nothing has been
submitted yet.  Written 2026-11-09 against CGSuite 2.1.1 and upstream commit
`e909ab8` (2024-01-15).

## Summary

`TakeAndBreak(code).NimValueSequence(n)` in CGSuite is about **13× slower** than it
needs to be for plain octal and hexadecimal codes.  A ~60-line fast path in
`NimValueSequence.java` makes it as fast as octal-explorer's C code.  That is a pure
Java change and gives the same results.

## Measurements

The CGSuite engine was called directly (`new TakeAndBreak(code).sequence().nimValues(n)`,
no REPL) and compared with the fast-path prototype in `FastOctalBench.java`.  Both ran
on CGSuite's bundled JRE (Java 17.0.6) on an AMD Ryzen 9 7900X, after a JIT warm-up.

| Game | n | CGSuite engine | Java fast path | speed-up | octal-explorer (C) |
|---|---|---|---|---|---|
| `0.007` Treblecross | 100,000 | 19.74 s | 1.47 s | 13.4× | 1.40 s |
| `0.6` Officers | 100,000 | 19.74 s | 1.46 s | 13.5× | 1.41 s |
| `0.77` Kayles | 100,000 | 39.42 s | 2.91 s | 13.6× | 2.98 s |
| `0.F6` (hexadecimal) | 3,000 | 6.42 s | 0.66 s | 9.7× | 0.35 s |

Going through the REPL costs nothing extra: 18.4 s for `0.6` at n = 100,000 (see
`../results/`).  So all the overhead is inside `NimValueSequence`.

**Correctness.**  `FastOctalBench check all2 2000` compares the fast path with CGSuite
on **75 codes**.  These are all 1- and 2-digit octal codes, codes with d₀ (`4.0`,
`4.07`, `8.0`, `C.0`), hexadecimal codes (`0.F`, `0.F6`, `0.9`, `0.28`, `0.9C`), and
`0.137`, `0.007`, `0.106`.  The results are identical for all of them.

## Where the time goes

`NimValueSequence.calculateNimValue` (Java, by Michael Albert) is generic over any
`HeapRuleset`.  For every option of every heap size it:

1. Calls `TBCode.TBTraversal.advance()`, a general partition generator for generalized
   codes (`!`, `?`, `=`, `{2+}`, additive digits, repeating parts).  Even for a plain
   octal digit it does a `match` on a Scala `Enumeration` constraint, multiplications
   in `nextValidHeapSize`, and a redistribution loop in `distributeTokens`, which has an
   `assert` on each call.
2. Reads the option through the `Traversal` interface (`currentLength`,
   `currentPart(i)`).
3. Checks `part < position` and `UNDEFINED_VALUE` for every part.  This supports
   options larger than the heap, which plain codes never have.
4. Sets a bit in a `java.util.BitSet`, which `clear()` empties for every heap size.

The loop that is actually needed is one array load, one XOR and one store per option.

## Proposed change

`cgsuite-fastpath.patch` is a unified diff against upstream `e909ab8`, apply with
`git apply` in the cgsuite repository.  It touches two files:

- **`TakeAndBreak.scala`**: new `def fastPathDigits: Array[Int]`.  It returns the
  numeric code digits when `tbCode.isHexadecimal`, there are no additive digits, no
  repeating part (`period == 0`), and d₀ allows neither 0 nor 1 heaps.  Otherwise it
  returns `null`.
- **`NimValueSequence.java`**: the constructor asks `TakeAndBreak` for the fast-path
  digits.  If there are any, `calculateNimValues` uses a new `calculateNimValuesFast`:
  - Options are enumerated directly: for each digit k, two-heap splits `a + b = h − k`,
    three-heap splits for hex digits, and the 0- and 1-heap cases.
  - The mex uses an `int[]` **stamp table**, where `stamps[v] == h` means v is an option
    value of h.  It never needs clearing and is grown to twice the next power of two
    above the largest value.
  - Cancellation (`Thread.interrupted()`) is checked every 4,096 heap sizes, so the GUI
    cancel button keeps working.
  - Everything else (generalized codes, other `HeapRuleset`s, `checkPeriodicity`) is
    unchanged.

Verification so far:

- **Algorithm:** identical to CGSuite on 75 codes (`FastOctalBench.java`, above).
- **The patched `NimValueSequence.java`:** compiles against `cgsuite-core.jar` with a stub
  for `TakeAndBreak.fastPathDigits`.  It has the same 4 unchecked-cast warnings as the
  original code.
- **Not yet done:** building CGSuite with the patch (sbt) and running its test suite.
  This is the first step before opening a PR.

## Possible further improvements (not in the patch)

- `HeapRuleset.nimValueSequence` boxes every value into `SmallInteger` and copies
  arrays.  That is negligible next to the computation.
- Two-heap splits could be parallelised for very large n.  That needs a reduction of
  stamp tables per thread, which is more complex.
- The same direct enumeration would help `heapOptions` callers.  Only
  `NimValueSequence` matters for speed, though.

## Draft for the issue

> **NimValueSequence: ~13× speed-up possible for plain octal/hexadecimal codes**
>
> `TakeAndBreak("0.6").NimValueSequence(100000)` takes ~19.7 s on a Ryzen 9 7900X.
> Most of the time is spent in the generic `TBTraversal` partition generator and in
> `BitSet` handling, not in the mex itself.  A fast path for codes that are plain
> octal/hex (no generalized digits, no additive digits, no repeating part) brings it
> to ~1.5 s with identical results, checked on 75 codes.  Proposed patch:
> `TakeAndBreak.fastPathDigits` + `NimValueSequence.calculateNimValuesFast` with a
> stamp-table mex.  Benchmark and patch attached.  Happy to open a PR.

## Reproduce

```sh
CP=~/CGSuite/CGSuite/modules/ext/cgsuite-core.jar
javac --release 17 -d /tmp/fastoctal -cp $CP FastOctalBench.java
~/CGSuite/jre/bin/java -Xss64m -cp $CP:/tmp/fastoctal FastOctalBench check all2 2000
~/CGSuite/jre/bin/java -Xss64m -cp $CP:/tmp/fastoctal FastOctalBench bench 0.007,0.6,0.77 2000,20000,100000
```

`javac` comes from a system JDK, since CGSuite's bundled JRE has no compiler.
