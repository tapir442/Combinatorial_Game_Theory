-- Turn ```math fenced blocks (GitHub's display-math syntax) into display math,
-- so the same Markdown renders on GitHub and in the pandoc/LaTeX PDFs.
function CodeBlock(el)
  if el.classes[1] == "math" then
    return pandoc.Para({ pandoc.Math("DisplayMath", el.text) })
  end
end
