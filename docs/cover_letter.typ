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

Concerning hiring at FUTO,

#v(1em)

I align with FUTO's mission to deliver alternatives to large and exploitative digital platforms,
and I admire FUTO's commitment to open source, as well as their efforts to sustainably fund their
open source development.

My skills can contribute towards that mission. I have work experience with modern web development,
browser automation, and server infrastructure. I keep up-to-date with browser adblocking and
privacy tools, and have a strong understanding of how they work. With these skills, I can
confidently transition into deobfuscation of adversarial front-ends or server API's. I have over a
year of experience in C\#, and minor use of Kotlin on hobby projects. I have studied video codecs
and streaming protocols. These skills make me a fit for your Grayjay Plugin Engineer role.

I am open to all other roles, if they seem to be a better fit for me. I use Linux as a daily-driver
and use open source, home-server software regularly. I'm currently writing plug-ins that add
procedural generation and voxel features to the open source Bevy game engine, in Rust. My graduate
research involved machine learning and computer vision using Python and PyTorch, and my degree
coursework focused on high performance computing, visualization, and image processing. I am
confident in my ability to dive deep and learn all kinds of software development, and I have strong
written communication and documentation skills required for an async, low-process team. I'm eager
to grow and take on new responsibilities.

Let's work together!

#v(1em)

Best,

James Youngblood
