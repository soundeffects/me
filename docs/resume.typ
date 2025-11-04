// Styling
#let darkgray = luma(80)
#set text(
  font: "New Computer Modern"
)
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

Sofware and Graphics Engineer

== Education
=== M.S. in Computing: Graphics and Visualization,
University of Utah, 2019 - 2025
- Classes:
- GPA: 3.8, deans list, and awarded full-ride merit-based scholarship
- Skills:

== Portfolio
=== Prockit
- Skills:

=== Planets on wgpu
- Skills:

=== More at soundeffects.github.io/me

== Research

=== Digital Image Transformations Degrade Gaze Prediction Accuracy,
2023 - 2025
- Thesis
- Skills:

=== BYU Camacho Lab,
2021
- Volunteer
- Skills:

== Employment
=== Teaching Assistant,
University of Utah, 2020 - 2024
- Responsibilities:
- Skills:

=== Contractor,
Crescendo Technologies, Inc., 2023
- Responsibilities:
- Skills:

=== Intern,
Verisage Custom Software, 2018-2019
- Responsibilities:
- Skills:
