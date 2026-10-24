# My lil Toolbox

VCV Rack 2 modules by A_Development. MIT.

Jack rings: blue = clock, reset and triggers; lime = pitch; magenta = CV modulation.

## Arpeggio

A dual quantizer / arpeggiator. Sections A and B share one scale.

**Scale.** The 12 keys toggle notes in or out of the scale. TYPE loads a preset into the keys (major, natural minor,
harmonic minor, dorian, phrygian, lydian, mixolydian, major pentatonic, minor pentatonic, chromatic); TYPE CV adds
1 V per preset. Your key edits stay until TYPE moves to another preset. RAND (button or trigger) rolls a random scale
that always keeps C. With every key off, CV is not quantized (TRIG still samples it, AXIS still shifts it).

**Modes.** MODE switches both sections between quantizer (lime) and arpeggiator (magenta).
- Quantizer: with TRIG unpatched, CV is quantized continuously and TRIG out fires on each new note. With TRIG patched,
  CV is sampled on each trigger; TRIG out still fires only when the note changes. Context menu "Triggers pass through (quantizer mode)":
  TRIG out fires on TRIG in instead.
- Arpeggiator: each trigger steps through the scale notes from the CV-in note upward across OCT octaves (1-4).
  TRIG out passes each trigger. DIR cycles up, down, up-down, random (random never repeats a note twice in a row).
  RESET (shared by A and B) makes the next trigger play the pattern's first note (the top note when DIR is down).

**Pitch.** INTERVAL moves B by -12..+12 *scale degrees*, so B harmonizes in key (CV: 1/12 V per degree).
AXIS transposes both outputs by -12..+12 semitones (CV: 1 V/oct) (knob + CV, -12..+12 total). Outputs clamp to +-10 V.

**B follows A.** Unpatched B CV and B TRIG read A's, so with nothing in B you get A plus INTERVAL.

**Lights.** Dim keys are in the scale; lime is A's note, blue is B's (both colors when they share a key), shown before AXIS. The DIR lights show U, D, UD or RND.

## Ripple

Polyrhythm trigger generator. Four generators tick on a 16th-note grid, each every PERIOD sixteenths, and mix into two
complementary rhythms, R and R'.

- **TEMPO** 30-300 BPM; the CV input is exponential (+1 V doubles). Patch **EXT** to clock it from outside: one pulse
  per 16th, or per beat (context menu).
- **PLAY** and **RESET** buttons, each with a trigger input. RESET returns to the downbeat, where every generator lines up.
  On EXT, RESET waits for the next pulse. Loading a preset also returns to the downbeat.
- **A-D**: PERIOD knob (1-16 sixteenths; CV adds 1 per volt), ON button, and a trigger output that fires every PERIOD
  ticks.
- **MODE** (lime = GRID, magenta = WAVE) and **DENSITY** (1-4):
  - GRID: R fires on ticks where at least DENSITY generators hit; R' fires on every tick where R does not.
  - WAVE: each generator is a sine with its period; R fires when their average rises past a threshold set by DENSITY,
    R' when it falls past the negative threshold. Looser and off-grid.
- **CLOCK** fires every 16th (or every beat, context menu). **ACCENT** fires once per full cycle of the active periods.

## Build

MSYS2 mingw64 with the Rack SDK: `export RACK_DIR=/c/dev/Rack-SDK`, then `make test` (core checks), `make`,
`make install`. `tools/panel.py` regenerates the panel and `src/layout.hpp` (Python 3 + fontTools). `tools/rig` is a
dev-only Windows rig for an isolated Rack.
