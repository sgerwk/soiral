# SOIRAL

Receive or emit remote control codes through an adapter connected to the sound
card or serial port.

## Receive

The adapter is a single infrared photodiode connected to the microphone jack:
[the infrared receiver page](http://ststefanov.eu/?p=142&lang=en). It may be
soldered or assembled using breakouts. Also the el-cheapo sound emitter (the
one used by irblast, below) may work at short range; do not insert it fully in
the jack.

![sound receiver schematics](images/sound-receiver-schematics.png "sound receiver
schematics")

![sound receiver soldered](images/sound-receiver-soldered.jpg "sound receiver soldered")

![sound receiver breakout](images/sound-receiver-breakout.jpg "sound receiver breakout")

![sound receiver breakout parts](images/sound-receiver-breakout-parts.jpg "sound receiver breakout parts")

![sound receiver partial insertion](images/sound-receiver-partial.jpg "sound receiver with partial insertion")

The following programs use this adapter:

- **remote** decodes the signals received from a remote control

- **layout** stores all codes a remote control emits in a text file;  
  **layouttoirdb** converts it into the
  [irdb](https://github.com/probonopd/irdb) format

- **signal2pbm** visualizes the raw infrared signals

## Send

Two adapters send remote control signals, one through the sound card, the other
through the serial port. The first is the universal remote control for mobiles
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

![serial emitter breadboard connections](images/serial-emitter-breadboard-connections.png "serial emitter breadboard")

![serial emitter breadboard](images/serial-emitter-breadboard.jpg "serial emitter breadboard")

![serial emitter adapter](images/serial-emitter-adapter.jpg "serial emitter adapter")

