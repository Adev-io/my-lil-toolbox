# My lil Toolbox

VCV Rack 2 modules by A_Development. MIT.

## Arpeggio

A dual quantizer / arpeggiator. Sections A and B share one scale.

**Scale.** The 12 keys toggle notes in or out of the scale. TYPE loads a preset into the keys (major, natural minor,
harmonic minor, dorian, phrygian, lydian, mixolydian, major pentatonic, minor pentatonic, chromatic); TYPE CV adds
1 V per preset. Your key edits stay until TYPE moves to another preset. RAND (button or trigger) rolls a random scale
that always keeps C. With every key off, CV passes through unquantized.

**Modes.** MODE switches both sections between quantizer (green) and arpeggiator (amber).
- Quantizer: with TRIG unpatched, CV is quantized continuously and TRIG out fires on each new note. With TRIG patched,
  CV is sampled on each trigger; TRIG out still fires only when the note changes. Context menu "Triggers pass through":
  TRIG out fires on TRIG in instead.
- Arpeggiator: each trigger steps through the scale notes from the CV-in note upward across OCT octaves (1-4).
  DIR cycles up, down, up-down, random (random never repeats a note twice in a row). RESET makes the next trigger
  play the pattern's first note (the top note when DIR is down).

**Pitch.** INTERVAL moves B by -12..+12 *scale degrees*, so B harmonizes in key (CV: 1/12 V per degree).
AXIS transposes both outputs by -12..+12 semitones (CV: 1 V/oct). Outputs clamp to +-10 V.

**B follows A.** Unpatched B CV and B TRIG read A's, so with nothing in B you get A plus INTERVAL.

**Lights.** Dim keys are in the scale; green is A's note, blue is B's.

## Build

MSYS2 mingw64 with the Rack SDK: `export RACK_DIR=/c/dev/Rack-SDK`, then `make test` (core checks), `make`,
`make install`. `tools/panel.py` regenerates the panel and `src/layout.hpp` (Python 3 + fontTools). `tools/rig` is a
dev-only Windows rig for an isolated Rack.
