# TFT Touch UI — Build Plan (Phase 2: UI)

Phase 1 (display + touch hardware bring-up, oversampled/averaged touch
reads) is complete. This document scopes the actual UI build.

## Architecture: two Teensys, linked over serial/UART

This TFT UI runs on its **own, separate Teensy 4.1** from the drum
controller. It is not a second role bolted onto the drum sketch — it's a
standalone display/input device that talks to the drum Teensy over a
UART link to read and write the drum sketch's live tuning variables
(thresholds, curve settings, calibration state, per-pad status).

**Why this matters for the build:** every "live-adjust X" feature in this
plan requires a message going out over serial to the drum Teensy, and
(for anything read back, like current threshold values or hit-monitor
feedback) a message coming back. None of this can just read/write a local
variable — there's actual round-trip communication involved. Keep this in
mind for latency: UI interactions that fire off a serial command should
still feel responsive on-screen (e.g. optimistic UI updates), rather than
visibly waiting on a round trip for every slider tick.

**Not yet decided / needs its own follow-up conversation before building
the comms layer:**
- Physical UART pins on each Teensy, and wiring between them
- Message protocol — suggest starting simple (newline-delimited
  human-readable commands, similar in spirit to the existing Serial
  Monitor `c` / `e <num>` / `p` commands the drum sketch already
  supports) rather than inventing a binary protocol right away
- Whether the drum sketch needs new commands added to expose per-pad
  threshold/curve values individually (currently some of this is
  set-only via constants, not runtime-adjustable or queryable)

**For this phase of UI work:** build the screens and their local
interaction logic first, with the serial calls stubbed out (e.g. a
`sendCommand(String cmd)` function that just prints to the TFT Teensy's
own Serial for now). Wire up the actual UART protocol as a distinct next
step once both sides agree on the message format — don't let protocol
design block screen/layout work.

## Screen flow

```
Splash screen
   |
   v
Landing page (4x4 pad matrix + footer)
   |
   +--> Pad Assignment
   |
   +--> Tuning
          +--> Sensitivity / threshold
          +--> Velocity curve editor
          +--> Crosstalk / chord settings
          +--> Calibration flow
   |
   +--> (Hit Monitor — out of scope for now, footer icon can exist but
   |      lead nowhere / show "coming soon")
   |
   +--> (Settings — out of scope for now, same treatment)
```

Only **Pad Assignment** and **Tuning** get built out this phase. The other
two footer icons should still be visually present (so the footer layout is
final and doesn't need redesigning later) but can be disabled/greyed or
show a simple placeholder screen.

## Screen 1: splash screen

- Full black background
- Centered text (horizontally and vertically): **"Initializing Jaiba Hexa
  Drum UI"**
- No interaction — this is a fixed-duration or "wait for boot" screen,
  then auto-advances to the landing page
- Suggest ~1.5–2 second minimum display time even if boot/init finishes
  faster, so it doesn't flash by unreadably fast

## Screen 2: landing page

**Layout, top to bottom:**
- Main content area: 4×4 grid of pad cells, representing the 16 future
  pads (currently only 6 are physically built — cells for unbuilt pads
  should probably be visually distinguished as "not yet installed" rather
  than looking identical to active ones, though the exact treatment is a
  design decision to make during implementation)
- Each cell: shows pad number, and eventually (once wired to hit-monitor
  data) could flash/highlight on a real hit — not required for this phase,
  but worth keeping the cell component flexible enough to support that
  later without a rewrite
- Footer: 4 icon buttons in a row — **Pad Assignment, Tuning, Hit Monitor
  (disabled), Settings (disabled)**

**Interaction for this phase:**
- Tapping a pad cell while on the landing page: no defined behavior yet —
  decide whether tapping a cell here should jump straight into Pad
  Assignment for that specific pad, or whether Pad Assignment is only
  reached via the footer icon and has its own internal pad picker. Worth
  deciding before Claude Code builds it, so the matrix component's tap
  handler is written once, correctly, rather than retrofitted.

## Screen 3: Pad Assignment

**Purpose:** tap a pad, choose what it triggers.

**Suggested flow:**
1. Same or similar 4×4 matrix shown again (or reuse the landing page's
   matrix component), user taps a pad to select it
2. Selected pad highlighted; a detail panel appears showing current
   assignment (currently: fixed MIDI notes C4–A#4 per the existing drum
   sketch's `scaleNotes[]` array)
3. Some control to change the assignment — simplest version: a note
   picker (e.g. +/- buttons or a small keyboard widget) to set which MIDI
   note that pad sends
4. A "save" or immediate-apply action, which sends the new mapping to the
   drum Teensy over serial

**Open question to resolve before building:** does changing a pad's note
assignment need to persist across power cycles (i.e. does the drum Teensy
need to save it to EEPROM/flash), or is it fine for now if it resets to
the default `scaleNotes[]` array on reboot? This affects whether the drum
sketch needs a persistence layer added as a prerequisite.

## Screen 4: Tuning (sub-menu with 4 items)

**Design principle, applies to every parameter below:** each control must
visually show (a) which sensor it affects — piezo or velostat, consistently
color-coded across the whole UI (e.g. one color for piezo, a distinct one
for velostat) — and (b) *where* in that sensor's response behavior the
parameter acts. This came directly from real confusion during testing
(mixing up which pad/sensor a given tuning change applied to, no intuition
for what a threshold number "means" physically) — the goal is that someone
looking at any Tuning screen understands the effect before touching
anything, not just after.

Two diagram families cover all four sub-screens:

- **Curve-position diagrams** (threshold, ceiling baseline, curve exponent):
  all three live on the same x-axis = input intensity, y-axis = output
  velocity (0–127) graph. Threshold and ceiling baseline are draggable
  markers on that x-axis (left edge = "nothing below this fires", right
  edge = "this and above always hits 127"); curve exponent changes the
  actual line shape between them. These three could reasonably share one
  visual component across their sub-screens rather than needing separate
  graphs.
- **Timing/relative diagrams** (crosstalk ratio/window): NOT a curve —
  these concern relationships between simultaneous hits across pads, not
  one pad's response shape. Needs its own visualization: something like a
  timeline showing a primary hit, the crosstalk window as a shaded region
  after it, and the ratio threshold as a line separating "counts as a real
  chord note" from "discarded as bleed-through" — with an example
  secondary hit shown landing above or below that line.

This principle should also apply to whatever the eventual velostat tuning
phase adds (`TRIGGER_PCT`, `RELEASE_PCT`, etc.) — those are additional
curve-position-style markers on the velostat's own response graph.

Landing screen for this section shows 4 tappable rows/cards, one per
sub-feature below. Each opens its own screen.

### 4a. Sensitivity / threshold adjustment
- Per-pad view — likely a pad selector (reuse matrix or a simple
  list/dropdown) plus a slider or numeric stepper for that pad's
  `PIEZO_THRESHOLD`
- Also expose the adaptive ceiling baseline (`PIEZO_MAX`) per pad
- Consider showing the pad's *current* live raw reading alongside the
  threshold slider (if the serial link supports streaming live values) —
  this would make it much easier to set the threshold correctly by
  watching noise vs. real hits in real time, similar to what we did
  manually via Serial Monitor throughout this project. Not required for
  a first version, but flag as a strong candidate for a fast-follow.

### 4b. Velocity curve editor
- Visual curve display (the shape resulting from `PIEZO_CURVE_EXP`)
  plotted as a simple line graph: x-axis = input intensity (0–100%),
  y-axis = output velocity (0–127)
- Either a slider for the exponent directly, or buttons cycling through
  the existing curve presets (the drum sketch already has
  `curvePresets[]` for the velostat side — decide whether piezo curve
  editing reuses that preset list or has its own)
- Live preview: redraw the curve as the exponent/preset changes, before
  committing/sending to the drum Teensy

### 4c. Crosstalk / chord settings
- Two controls: `CROSSTALK_RATIO` (percentage, e.g. 0–100% slider) and
  `CROSSTALK_WINDOW` (milliseconds, small numeric range e.g. 1–20ms)
- Worth a short on-screen explanation of what these do, since they're not
  self-explanatory from the name alone (e.g. "how similar a second hit's
  strength must be, within this time window, to count as a chord instead
  of crosstalk")

### 4d. Calibration flow
- Trigger button that sends the equivalent of the existing serial `c`
  command to start the velostat calibration sequence
- Screen should mirror the calibration FSM's existing states so the user
  gets on-screen feedback instead of needing Serial Monitor open:
  "Resting — don't touch any pad" → "Touch pad to calibrate" → "Pad N
  calibrated, touch next" → "Calibration complete"
- This requires the drum Teensy to report its calibration state changes
  back over serial as they happen, not just accept the trigger command —
  worth flagging as a protocol requirement once the comms layer is
  designed
- Per-pad piezo calibration doesn't exist yet in the drum sketch (it's
  still a "possible next step" per the drum project's own CLAUDE.md) — this
  screen should be built to accommodate it once it exists, but only needs
  to trigger velostat calibration for now

## Deferred: velostat tuning (not yet scoped into the screens above)

Everything in section 4 above (Tuning) currently only covers piezo-side
parameters. The velostat side has its own separate set of tunable values
with no UI or protocol representation yet: `curveExponent` (velostat's own
curve, distinct from `PIEZO_CURVE_EXP`), `TRIGGER_PCT`/`RELEASE_PCT`
(note-start/note-end sensitivity), `VEL_SMOOTHING`, `ATTACK_WINDOW_MS`, and
`MIDI_UPDATE_INTERVAL` (aftertouch rate). All are currently global constants
in the drum sketch, not per-pad and not exposed over UART.

**Deliberately deferred** until after the TFT project is wired up to the
current (piezo-only) protocol. When this phase starts: decide which of
these should become per-pad (mirroring the `PIEZO_THRESHOLD` refactor) vs.
staying global, add corresponding `UART_PROTOCOL.md` commands, and likely
add a 5th Tuning sub-screen (or extend existing ones) for velostat feel.

## Suggested build order for Claude Code

1. Splash screen (simplest, no interaction, good first milestone)
2. Landing page shell: 4×4 matrix rendering + footer with all 4 icons
   (2 active, 2 disabled/placeholder) — no serial logic yet, just layout
   and navigation between screens
3. Pad Assignment screen — UI and local state only, `sendCommand()` stub
4. Tuning sub-menu shell (4 rows, navigation into each) — placeholders OK
   for the 4 sub-screens initially
5. Build out 4a–4d one at a time, each with local UI first, serial stub
   calls in the right places
6. Only after all screens exist and navigate correctly: design and
   implement the actual UART protocol between the two Teensys, and wire
   the stubs to real commands
