# My lil Toolbox

VCV Rack 2 modules by A_Development. MIT.

Jack rings: blue = clock, reset and triggers; lime = pitch; magenta = CV modulation.

## My lil Arpeggio

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

**Gates.** Context menu "TRIG outs send gates (half the clock period)": TRIG out goes high on each trigger for half
the time since the last one (10 ms for the first, 1 ms..2 s), for voices that hold a note on a gate. Off by default.

**B follows A.** Unpatched B CV and B TRIG read A's, so with nothing in B you get A plus INTERVAL.

**Lights.** Dim keys are in the scale; lime is A's note, blue is B's (both colors when they share a key), shown before AXIS. The DIR lights show U, D, UD or RND.

## My lil Ripple

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
- **ENV** (right-hand column, an ADSR for a VCA): ATTACK 1 ms-5 s, DECAY 1 ms-5 s, SUSTAIN 0-100 %, RELEASE 1 ms-10 s,
  and **LEN** (1-16 sixteenths). Each hit of the chosen source retriggers the attack from the current level and holds
  the gate for LEN sixteenths; the ENV out is 0-10 V. Context menu "ENV fires on": R (default), R', ACCENT, A, B, C or
  D. In WAVE mode the held length reads LEN to LEN+1 sixteenths (never shorter than LEN; LEN 16 is capped at 16),
  because R and R' land between ticks. A hit while the gate is still held re-arms it, so the VCA only closes when LEN is
  shorter than the gap between hits: in GRID mode a dense rhythm (the default A 2 + B 3 on R hits every 1-2 sixteenths)
  keeps ENV open at any LEN of 2 or more; in WAVE it takes LEN 3 or more. Turn LEN down, or thin the rhythm (DENSITY,
  fewer generators), for a gap. The gate counts clock ticks: if an EXT clock stops mid-gate, ENV holds at SUSTAIN until
  PLAY is toggled or RESET. Ripple is 16 HP since 2.2.0 (was 12): in an old patch, a module right next to Ripple may now
  overlap it.

## My lil Filter

The acid 24 dB/oct resonant low-pass of My lil Acid Bass, for any audio: two cascaded state-variable stages with the
same resonance and level behavior as that voice.

- **CUTOFF** 60-8000 Hz (the Acid Bass knob). **RES** rings hard at the top but stays bounded; like Acid Bass, the
  level drops as RES rises. **DRIVE** saturates the input (tanh) before the filter: at 0 it is clean, and at any
  setting a 5 V peak stays near 5 V while quieter signals get louder and grittier.
- **CUT CV** with its **AMT** attenuverter (default 100%): 10 V sweeps the whole CUTOFF range. **V/OCT** makes the
  cutoff track pitch at 1 V/oct (0 V = no change). **RES CV**: 10 V = the full RES range.
- **IN** / **OUT**: polyphonic. Each channel has its own filter and OUT has as many channels as IN. Mono CV moves every
  channel; poly CV moves each channel on its own. OUT bends softly above 5 V and never exceeds 10 V.

## My lil Slider

Two 8-step slider sequencers, A and B, on one clock, with a gate button under every slider. 18 HP.

- **CLOCK** steps both sections; **RESET** makes the next clock play step 1. The slider handle lights on the step each
  section is playing.
- **Sliders** set each step's CV; the **gate buttons** under them choose which steps fire a gate. **GATE** out is high
  for half the clock period (10 ms before a period is known), only on steps whose button is on.
- Per section: **LEN** (1-8 steps; A and B can differ, so they drift against each other as a polyrhythm),
  **RNG** (0-1, 0-2, 0-5, -5..+5 or 0-10 V; default 0-5), **INV** (flips the voltage about the middle of the range),
  **REV** (plays the section's steps backwards).
- **CHAIN** runs one playhead through A's steps then B's (LEN A + LEN B steps, up to 16). REV still applies inside each
  section. The section that is not playing holds its last CV with its gate low.
- **REC** (per section): on every clock the section's **CV** input is written into the step being played (the slider
  moves to it) and its **GATE** input (high at 1 V or more) sets that step's button. It overwrites as it passes; disarm
  it and the section loops what it recorded. INV is applied on the way out, so a recording is stored as it came in.
  Unpatched B CV and B GATE read A's.
- Context menu **Snap CV to semitones** rounds both CV outs to the nearest 1/12 V.

Patch idea: Slider A CV into a voice's V/OCT and A GATE into its GATE; set a few sliders and gate buttons, or arm REC
and play a pitch CV into A CV for one pass to capture a line.

## My lil Mixer

Eight stereo strips into a master bus, with two sends and returns, one sidechain that ducks every strip, and meters
that show what reaches the audio interface. 34 HP.

- Per strip: inputs **L** and **R** (unpatched R copies L; poly cables are summed, with no clipping before LEVEL), **LEVEL** fader (0 dB at the default,
  +6 dB at the top), **PAN** (a mono source pans at constant power, a stereo source balances), **SEND 1** and **SEND 2**,
  **DUCK**, **M** (mute) and **S** (solo). Any solo plays only the soloed strips; mute always wins.
- Meters read **dBFS against 10 V**: 10 V is 0 dBFS, the level Rack's Audio module sends to the interface as full scale.
  Beside each fader: an LED ladder of the strip's input (before LEVEL and the duck, so a muted or turned-down strip still
  shows what arrives), -60 to 0 dBFS, lime below -6 dBFS (5 V), magenta above, a white tick holding each peak for 1.5 s;
  the dot on top latches magenta when the input reaches 10 V (click the meter to clear it); and a blue bar showing how
  far the sidechain pulls the strip down right now (0 to -24 dB). The face over each strip talks with what the strip
  sends on (after LEVEL, mute and the duck: a muted strip keeps a straight face).
- The display over the master: OUT L and R after the soft clip on the same ladders, with the master clip dot; their
  peak-hold readouts in dBFS; the short-term loudness (LUFS over 3 s, K-weighted as in BS.1770); and the master's last
  6 s of peaks, kicks standing tall and ducks showing as notches. Click the display to clear the master's and every
  strip's clip latch; Initialize clears them too, and drops the meter bars and peak ticks.
- **SC** feeds the sidechain: each strip's **DUCK** sets how far it is pulled down when the sidechain hits, **REL** how
  fast it comes back (20 ms to 1 s). Patch the kick into SC and turn up DUCK on the bass and pads for the house pump;
  leave the kick's own strip at 0.
- **SND1** and **SND2** are stereo sends, **RTN1** and **RTN2** stereo returns summed into the master at unity
  (unpatched R copies L). **MASTER** sets the output level; **OUT L** and **OUT R** are the mix. Sends are not clipped.
- Context menu: **SC IN reads** Audio (an envelope follower, the default) or Triggers (each rising edge fires one
  duck, so the pump stays even whatever the kick's level); **Sends pre-fader** (off: post-fader; pre-fader sends ignore LEVEL, PAN and mute); **Master soft clip**
  (on by default: untouched up to 5 V, then a smooth bend that never passes 8 V).

Patch idea: Drums kick into strip 1 and into SC, an acid line into strip 2 with DUCK at 70 %, a pad into strip 3 with
SEND 1 into a reverb, the reverb back into RTN1, OUT L/R to the audio interface.

## Build

MSYS2 mingw64 with the Rack SDK: `export RACK_DIR=/c/dev/Rack-SDK`, then `make test` (core checks), `make`,
`make install`. `tools/panel.py` regenerates the panel and `src/layout.hpp` (Python 3 + fontTools). `tools/rig` is a
dev-only Windows rig for an isolated Rack.
