# SOIRAL

Receive or emit remote control codes through an adapter connected to the sound
card or serial port.

## Receive

The adapter is a single infrared photodiode connected to the microphone jack:
(http://ststefanov.eu/?p=142&lang=en "the infrared receiver page").
It may be soldered or assembled using breakouts.

![sound receiver schematics](images/sound-receiver-schematics.png "sound receiver
schematics")

![sound receiver soldered](images/sound-receiver-soldered.jpg "sound receiver soldered")

![sound receiver breakout](images/sound-receiver-breakout.jpg "sound receiver breakout")

![sound receiver breakout parts](images/sound-receiver-breakout-parts.jpg "sound receiver breakout parts")

The following programs use this adapter:

- **remote** decodes the signals received from a remote control

- **layout** stores all codes a remote control emits in a text file

- **signal2pbm** visualizes the raw infrared signals

## Send

Two adapters send remote control signals, one using the sound card and the
other the serial port. The first is the universal remote control for mobiles
that is sold for under a Euro on ebay; it is cheap and can be bought
ready-made, but it may work with the **irblast** program here or not, or only
at close range, depending on the sound card and the device to be controlled.
The serial port adapter needs to be made from parts, but is more reliable; it
can be assembled on a breadboard.

- **irblast** emits codes via the sound adapter

![sound emitter schematics](images/sound-emitter-schematics.png "sound emitter
schematics")

![sound emitters](images/sound-emitter-both.jpg "sound emitters")

![sound emitter opened](images/sound-emitter-opened.jpg "sound emitter opened")

- **serial** emits codes via the serial adapter

![serial emitter schematics](images/serial-emitter-schematics.png "serial emitter
schematics")

![serial emitter breadboard](images/serial-emitter-breadboard.png "serial emitter breadboard")

![serial emitter adapter](images/serial-emitter-adapter.png "serial emitter adapter")

