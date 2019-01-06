PNG=sound-receiver-schematics.png sound-emitter-schematics.png \
serial-emitter-schematics.png serial-emitter-breadboard-connections.png
SVG=${PNG:.png=.svg}

all: ${PNG} ${SVG}

%.png: %.fig
	fig2dev -L png -m 2 $< $@

%.svg: %.fig
	fig2dev -L svg -m 2 $< $@

clean:
	rm -f ${PNG} ${SVG}

