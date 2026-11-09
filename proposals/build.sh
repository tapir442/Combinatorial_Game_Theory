#!/usr/bin/env bash
# Build PDFs of the CGT PhD proposals: one per proposal plus a combined booklet.
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p pdf
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

opts=(
  --pdf-engine=xelatex
  -V mainfont="DejaVu Serif" -V sansfont="DejaVu Sans" -V monofont="DejaVu Sans Mono"
  -V mathfont="DejaVu Math TeX Gyre"
  -V geometry:a4paper -V geometry:margin=2.3cm -V fontsize=10pt
  -V colorlinks=true -V linkcolor=blue -V urlcolor=blue
  -V date="9 November 2026"
  --lua-filter=tex/math-fence.lua   # ```math blocks (GitHub syntax) -> display math
  -H tex/macros.tex                 # \lt, \gt (GitHub-safe < and >)
)

# Sources section (shared by every single-proposal PDF)
sed -n '/^## Sources/,$p' src/99-further-and-sources.md > "$tmp/sources.md"

for f in src/0[1-6]-*.md; do
  name=$(basename "$f" .md)
  title=$(head -1 "$f" | sed 's/^# //')
  # Drop the H1 (used as the document title) and append the sources
  { tail -n +2 "$f"; echo; cat "$tmp/sources.md"
    # Proposal 1 builds on the octal-games background chapter: append it
    if [ "$name" = 01-octal-periodicity ]; then echo; cat src/90-background-octal-games.md; fi
  } > "$tmp/$name.md"
  pandoc "$tmp/$name.md" "${opts[@]}" \
    -V title="$title" -V subtitle="PhD proposal in Combinatorial Game Theory" \
    -o "pdf/$name.pdf"
  echo "built pdf/$name.pdf"
done

# Background chapter on its own
tail -n +2 src/90-background-octal-games.md > "$tmp/bg.md"
pandoc "$tmp/bg.md" "${opts[@]}" \
  -V title="What is known about octal games" -V subtitle="Background for the CGT PhD proposals" \
  -o pdf/90-background-octal-games.pdf
echo "built pdf/90-background-octal-games.pdf"

pandoc src/00-overview.md src/0[1-6]-*.md src/90-background-octal-games.md src/99-further-and-sources.md "${opts[@]}" \
  --toc -V toc-title="Contents" --number-sections=false \
  -V title="PhD Proposals: Open Problems in Combinatorial Game Theory" \
  -V documentclass=report -V classoption=openany \
  --top-level-division=chapter \
  -o pdf/CGT-PhD-proposals-all.pdf
echo "built pdf/CGT-PhD-proposals-all.pdf"
