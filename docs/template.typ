#let report(
    title: "",
    author: "",
    date: "",
    body,
) = {
    set document(title: title, author: author)
    set page(
        paper: "a4",
        margin: (top: 2.5cm, bottom: 2cm, x: 2.2cm),
        numbering: "1",
    )

    set text(font: "IBM Plex Sans", size: 10.5pt, lang: "en")
    set par(justify: true, leading: 0.62em)
    set heading(numbering: none)
    show heading: set text(font: "IBM Plex Sans Cond SmBld")
    show heading.where(level: 1): set text(
        size: 17pt,
    )
    show heading.where(level: 2): set text(
        font: "IBM Plex Sans Cond Medm",
        size: 13pt,
    )
    show heading: it => {
        v(0.4em)
        it
        v(0.3em)
    }

    show raw: set text(font: "IBM Plex Mono", size: 9pt)
    show math.equation: set text(font: "IBM Plex Math")
    show link: set text(fill: rgb("#2563eb"))

    // ---- title block: left-aligned, top of page, no rule ----
    text(font: "IBM Plex Sans Cond SmBld", size: 26pt)[#title]
    v(0.2em)
    text(size: 10pt, fill: luma(110))[
        #author · #date.display("[year]-[month]-[day]")
    ]
    v(1.5em)

    body
}
