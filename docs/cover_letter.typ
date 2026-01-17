// Styling
#let darkgray = luma(80)
#set text(
  font: "New Computer Modern"
)
#set page("us-letter")
#show heading.where(level: 1): set text(olive)
#show heading.where(level: 2): set text(olive)
#show heading: it => it.body
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

#v(3em)

To the hiring managers at X,

#v(1em)

I write this letter, and apply for your position X, because
I'm highly interested in...
