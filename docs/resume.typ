#let data = toml("resume_entries.toml")

// Styling
#let darkgray = luma(80)
#set text(
  font: "New Computer Modern"
)
#set page("us-letter")
#show heading.where(level: 1): set text(olive)
#show heading.where(level: 2): set text(olive)
#show heading: it => it.body
#show heading.where(level: 2): it => grid(
  columns: (auto, auto),
  gutter: 1em,
  it.body,
  align(horizon, line(length: 100%, stroke: olive))
)
#show link: set text(olive, weight: "semibold")
#show list.item: set text(darkgray)

= James Youngblood
#h(1fr)
#link("mailto:james@youngbloods.org")[
  
  #h(0.5em)
  #underline("james@youngbloods.org")
]
#h(1em)
#link("https://github.com/soundeffects")[
  
  #h(0.5em)
  #underline("soundeffects")
]

#data.pursuit

#for section in data.sections.sorted(key: section => -section.priority) {
  if section.priority >= 0 [
    == #section.title
    #if section.title == "Portfolio" [
      Visual demos of all my projects are at
      #link("https://soundeffects.github.io/me")[
        #underline("soundeffects.github.io/me")
      ]
    ]
    #for entry in section.entries.sorted(key: entry => -entry.priority) {
      if entry.priority >= 0 [
        #grid(
          columns: (1fr, auto),
          row-gutter: 0pt,
          column-gutter: 0pt,
          [
            === #entry.title#if "organization" in entry [,] else [:]
            #if "organization" in entry [
              #entry.organization
            ] else [
              #entry.summary
            ]
          ],
          entry.year
        )
        #v(-5pt)
        #if "organization" in entry [
          #entry.summary
          #linebreak()
        ]
        #if "skills" in entry [
          Skills: #entry.skills.join(", ")
          #linebreak()
        ]
        #if "courses" in entry [
          Courses: #entry.courses.join(", ")
        ]
        #v(-2pt)
      ]
    }
  ]
}
