# Poor Man's Ambilight

I bought the lowboard for my TV second-hand.
To my surprise, there was an LED strip (with non-addressable LEDs) integrated, but with a defective controller.
I replaced it with an Arduino Nano and should have left it at that.
But unfortunately, I have to implement some of my ideas to make room in my brain for something new.
So I added a second strip behind the TV, expanded it with a color sensor, added a switch and a button, and moved the whole project form the Arduino to a microcontroller on a breadboard and put it in a case.

## Usage

I wanted to keep it as simple as possible, so I added only a single button to control everything, that is to switch between dynamic light mode and static light mode and within static light mode to change the color using the Hue, Saturation, and Value (HSV) color model.

- **Short press** (< 250 ms): Cycle through HSV parameter index in _Static_ mode 
- **Long press** (> 1 s): Sweeps through brightness in _Ambient_ mode or through selected HSV parameter in _Static_ mode
- **Inbetween press**: Toggle between _Static_ and _Ambient_ modes

## Parts List

1. Microcontroller [ATMEGA 328P-PU](https://www.reichelt.de/8-bit-atmega-avr-mikrocontroller-32-kb-20-mhz-pdip-28-atmega-328p-pu-p119685.html?CCOUNTRY=445&LANGUAGE=de&nbc=1&&r=1)
2. RGB Color Sensor [TCS34725](https://www.reichelt.de/entwicklerboards-rgb-farbsensor-tcs34725-debo-sens-color-p235477.html?CCOUNTRY=445&LANGUAGE=de&nbc=1&&r=1)
3. Power Supply [12V 3A](https://www.reichelt.de/steckernetzteil-36-w-12-v-3-a-mw-gst36e12-p171106.html?&trstct=pol_1&nbc=1)
9. Push Button [T 113A SW](https://www.reichelt.de/miniatur-drucktaster-0-5a-24vac-1x-ein-sw-t-113a-sw-p45167.html?CCOUNTRY=445&LANGUAGE=de&nbc=1&&r=1)
10. Power Switch [SPDT On-On](https://www.reichelt.de/miniatur-kippschalter-ein-ein-3-a-250-v-goobay-10020-p285987.html?&trstct=pol_0&nbc=1)
11. Color Sensor LED Switch [SPDT On-Off-On](https://www.reichelt.de/miniatur-kippschalter-ein-ein-3-a-250-v-goobay-10020-p285987.html?&trstct=pol_0&nbc=1) (intended for experimentation with the color sensor; should be skipped)
4. DC/DC Converter [TSR 1-2450](https://www.reichelt.de/dc-dc-wandler-tsr-1-1-w-5-v-1000-ma-sil-to-220-tsr-1-2450-p116850.html?CCOUNTRY=445&LANGUAGE=de&nbc=1&&r=1)
5. MOSFETs [IRLB 8721](https://www.reichelt.de/mosfet-n-kanal-30-v-50-a-rds-on-0-0065-ohm-to-220-irlb-8721-p200919.html?CCOUNTRY=445&LANGUAGE=de&nbc=1&&r=1) (3)
6. Crystal Oscillator [IQD LFXTAL003240](https://www.reichelt.de/standardquarz-grundton-16-mhz-iqd-lfxtal003240-p245409.html?CCOUNTRY=445&LANGUAGE=de&nbc=1&&r=1)
7. 22 pF Crystal Oscillator Capacitors [KERKO-500 22P](https://www.reichelt.de/keramik-kondensator-22-pf-10-npo-500-v-rm-5-kerko-500-22p-p9330.html?CCOUNTRY=445&LANGUAGE=de&nbc=1&&r=1)
8. 100 nF Decoupling Capacitor [KERKO 100N](https://www.reichelt.de/keramik-kondensator-100-nf-20-80-y5v-50-100-v-rm-5-kerko-100n-p9265.html?&trstct=pol_0&nbc=1)
9. DC Barrel Socket [5.5 mm / 2.1 mm](https://www.reichelt.de/einbaubuchse-zentraleinbau-aussen-5-6-mm-innen-2-1-mm-hebl-21-p8524.html)
10. A 72x50x26 mm³ case (I had no printer at the time; you definitely should go larger)

## Current Draw

When all LEDs are fully lit (the color is set to white), [a segment of 3 LEDs consumes 60 mA](https://learn.adafruit.com/rgb-led-strips/current-draw) from a 12 V power supply.
There are 10 segments per meter and I used a total of 4 meters.

    0.06 A * 10 segments/m * 4 m = 2.4 A

## Wiring

Please forgive me for not designing a proper schematic.

![pmAm Wiring](https://www.marclieser.de/data/content/interests/pmam/pmam_schematic.jpg)