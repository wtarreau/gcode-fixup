gcode-fixup
===========

This is a collection of simple tools used to fix and manipulate G-CODE files.
They are aimed for use with laser engravers and were not even tested with
anything else. I am using them to significantly improve raster images in
the laser mode provided with GRBL 1.1 on my EleksMaker A3 Pro, but also to
fixup some PCB-GCODE output to render correctly when facing dimension issues.

Functions like scaling in/out, mirroring, applying offsets, measuring the work
size, scaling the beam intensity (spindle rate), applying a gamma correction,
or optimizing the stream for laser mode so as to never stop are implemented.

There is no dependency beyond GNU awk which is present in any even minimalistic
Linux distribution and should be portable enough to other awk implementations.
It doesn't seem to contain any bashism though it that were the case they should
be trivial to fix, hence bash is not considered as a dependency.

Raster mode is way faster because adjacent segments are merged with no modal
state change so that the spindle speed only changes on the G1 commands. Thus
the beam scans from left to right and right to left only adjusting its
intensity. It can become very fast and the resolution is most only limited by
the PWM frequency, the laser power and the material being engraved. Engraving
at 2000 mm/min on wood with a 20W laser does works but at 1kHz pwm some dots
start to appear.

An example of use with screen captures is described here :
   https://wtarreau.blogspot.com/2019/09/quick-and-clean-pcbs-for-week-end.html
