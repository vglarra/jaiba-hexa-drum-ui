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
Landing page (9-tile hex layout, matching physical shell + footer)
   |
   +--> Tap a populated tile --> per-pad action menu
   |       +--> Assign note
   |       +--> Calibrate this pad (single-pad CAL_START)
   |       +--> View info
   |       +--> Adjust threshold/ceiling
   |
   +--> Tuning (footer icon)
          +--> Sensitivity / threshold
          +--> Velocity curve editor
          +--> Crosstalk / chord settings
          +--> Calibration flow (ALL-scope, or picks a pad then SINGLE)
   |
   +--> (Hit Monitor — out of scope for now, footer icon can exist but
   |      lead nowhere / show "coming soon")
   |
   +--> (Settings — out of scope for now, same treatment)
```

Both the per-tile menu's "Calibrate this pad" and the Tuning section's own
Calibration flow ultimately drive the same underlying mechanism — a
`CAL_START` call and live `CAL_STATE`-driven highlighting on the hex
component (see Screen 2 below). They're just two different entry points
into calibration: one already knows which pad (tap-driven), the other
starts broader (`ALL` scope, or picks a pad as part of the flow).

Only the hex layout landing page and the per-tile action menu are the
current build focus. Tuning's 4 sub-screens and the other 2 footer icons
remain as previously scoped — see below.

## Screen 1: splash screen

- Full black background
- Centered text (horizontally and vertically): **"Initializing Jaiba Hexa
  Drum UI"**
- No interaction — this is a fixed-duration or "wait for boot" screen,
  then auto-advances to the landing page
- Suggest ~1.5–2 second minimum display time even if boot/init finishes
  faster, so it doesn't flash by unreadably fast

## Screen 2: landing page

**Tile-tap interaction redesigned** (supersedes the earlier "jumps directly
to Pad Assignment" decision): tapping a populated tile opens a **per-pad
action menu** instead of going straight to note assignment. Note assignment
is just one of several actions available for a given pad, not the only
thing tapping a tile can do. Menu options:

- **Assign note** — what "Pad Assignment" (Screen 3) used to be the only
  destination for; now one menu item among several
- **Calibrate this pad** — triggers `CAL_START,<sensor>,SINGLE,<padIndex>`
  for a single-pad calibration run, not the old "calibrate everything"
  flow. Sensor type (piezo/velostat) still needs picking — either a
  sub-choice in this menu item, or the menu offers "Calibrate (piezo)" and
  "Calibrate (velostat)" as two separate entries
- **View info** — current threshold/ceiling/curve/note for this pad (a
  `GET_PAD` query, displaying the `PADVAL` response)
- **Adjust threshold/ceiling** — direct `SET_THRESH`/`SET_CEILING_BASELINE`
  for this pad, without going through the full calibration flow

Tapping an empty tile (R00/R01/R04) — no menu, since there's nothing to
act on yet; possibly a "not installed" message, exact treatment TBD.

**Visual calibration guidance — ties Calibration directly to the hex
component, not just text status.** When "Calibrate this pad" runs (or the
Tuning section's `ALL`-scope calibration runs, once that screen exists),
the hex layout component should visually highlight whichever pad the drum
Teensy is currently waiting on or capturing, driven directly by
`CAL_STATE`'s `padIndex` field:
- `WAITING_TOUCH` (for `SINGLE` scope, target already known) or
  `CAPTURING,<padIndex>` → highlight that tile using the hex component's
  existing `selectedIndex` parameter (already built, see Screen 2's
  original hex layout component work)
- `PAD_DONE,<padIndex>` → could shift that tile's highlight color to
  indicate "done" briefly, before returning to normal
- This means calibration screens don't need their own separate visual
  representation of "which pad" — they reuse the same hex component
  already built, just driven by live `CAL_STATE` data instead of a tap

**Live hit visualization — the hex landing page's "idle" behavior.** When
not showing an action menu overlay or in calibration guidance mode, the
hex tiles react live to actual playing: whenever a pad's `HIT` message
updates `drumState`, briefly flash that tile (brighten for ~150-200ms,
fade back) using the same highlight mechanism as calibration guidance.
This effectively gives an early, lightweight version of the deferred "Hit
Monitor" screen for free — it doesn't need to be a separate screen, just
how the landing page behaves whenever nothing else is overlaid on it.

- **Zero added latency risk to actual playing** — the drum Teensy's `HIT`
  send is fire-and-forget, sent immediately after the MIDI note already
  went out, with no round-trip or acknowledgment. The TFT's visualization
  is fully decoupled from playing feel; the only thing that could lag is
  the TFT's own redraw keeping up with very fast rolls, which is a display
  polish concern, not a playing-feel one.
- **Chords work automatically** — each simultaneous hit is its own
  independent `HIT` message (the crosstalk filter already separates real
  chord notes from bleed-through on the drum side), so multiple tiles
  simply flash together when a chord lands, no special-case logic needed.
- **Color-code by source** — flash piezo-triggered hits (`HIT`'s `P` tag)
  one color, velostat-triggered (`V`) a different color, extending the
  same "which sensor, visually obvious" principle used for the Tuning
  parameter visualizations (see Screen 4's design principle). Since `HIT`
  already carries the source tag, this is close to free to add.

**Layout, top to bottom (otherwise unchanged from before):**

**Layout updated with real tile data**, pulled directly from the drum
project's Blender CAD design (`CLAUDE.md`'s "Physical pad mapping" table)
rather than an approximate row/offset description. The shell's right
hemisphere is 9 flat-top hex tiles (`HEX_FLAT_WIDTH=77mm`):

| Tile | x (mm) | y (mm) | Status | Sensor index |
|---|---|---|---|---|
| R00 | 38.5 | 100.0 | empty | — |
| R01 | 115.5 | 100.0 | empty | — |
| R02 | 0.0 | 33.3 | populated | 0 |
| R03 | 77.0 | 33.3 | populated | 1 |
| R04 | 154.0 | 33.3 | empty | — |
| R05 | 38.5 | -33.3 | populated | 3 |
| R06 | 115.5 | -33.3 | populated | 5 |
| R07 | 0.0 | -100.0 | populated | 4 |
| R08 | 77.0 | -100.0 | populated | 2 |

(Y grows upward in this table, CAD convention — flip sign for screen
coordinates, which grow downward. Left hemisphere, L00-L06, is a longer-term
build target per `CLAUDE.md` and not part of this UI yet — only the 9
right-hemisphere tiles are shown.)

**Top to bottom:**
- Main content area: hex-packed layout of the 9 tiles above, built as a
  reusable component (shared with Pad Assignment, see Screen 3) rather than
  landing-page-specific rendering. Hardcoded tile positions, no runtime hex
  grid math needed — these are fixed design-time coordinates.
- Each populated tile shows a **friendly 1-based number** ("Pad 1" through
  "Pad 6") — NOT the internal 0-based sensor index shown in the table
  above. Mapping: sensor index 0 → "Pad 1", index 1 → "Pad 2", etc. This
  distinction is handled at exactly one place in the TFT code (the hex
  component's own label rendering), not scattered — internal logic, the
  UART protocol, and the drum sketch all stay 0-indexed; only what's drawn
  on screen adds 1.
- Empty slots (R00, R01, R04) are visually distinguished (dimmed fill,
  dashed outline) — they're real, present shell positions, just not wired
  to a sensor yet.
- Footer: same 4 icon buttons as before — Pad Assignment, Tuning, Hit
  Monitor (disabled), Settings (disabled)

**Interaction:** tapping a pad cell here jumps directly into Pad Assignment
for that specific pad (this was previously left open in the plan — now
decided, since Pad Assignment reuses this same hex layout, direct
navigation is the natural behavior rather than a separate picker).

## Screen 3: Pad Assignment

**Usage pattern clarification (important for design):** this isn't a
"tweak anytime" control — in practice it's a setup step done once per
soldering session. The user solders a batch of new sensors onto the shell
(expected a handful more times while building out toward the full 16-tile
goal), assigns each newly-wired pad once, and then it stays stable until
the next soldering round. Worth designing around "fast, clear, focused
setup flow right after wiring something new" rather than optimizing for
frequent casual re-assignment.

**Reuses the landing page's hex layout component** rather than a separate
matrix — same visual positions, same friendly numbering, same underlying
tile data table above.

**Flow:**
1. Hex layout shown, tap a pad (or arrive here already having tapped one
   from the landing page)
2. Selected pad highlighted; detail panel shows current assignment
   (currently: fixed MIDI notes C4–A#4 per `scaleNotes[]`)
3. Note picker to change the assignment (+/- buttons or small keyboard
   widget)
4. Save/apply sends `SET_NOTE` to the drum Teensy

**Still open, from before:** does note assignment need to persist across
power cycles (EEPROM/flash on the drum Teensy), or is resetting to
`scaleNotes[]` defaults on reboot acceptable for now? Unchanged from the
original plan — still unresolved, still worth deciding before this screen
is wired to real commands.

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

**Substantially re-scoped from the original plan.** Two independent axes
now, both need to be chosen before starting a calibration run:

**Sensor type:** Piezo or Velostat — calibrated completely independently,
never together in one run.

**Scope:**
- **Single pad only** — calibrate just the one pad selected, leave every
  other pad's existing calibration untouched
- **Single pad, then copy to all** — calibrate one pad, then apply that
  same result to every other pad (a deliberate, explicit version of what
  the drum sketch's `abortToUniformCalibration()` currently does only as
  an incidental side effect of aborting mid-run)
- **All pads** — the existing full sequential flow (touch each
  uncalibrated pad in turn until all are done)

**⚠️ Major prerequisite, not yet done on the drum side:** velostat
calibration already has a working auto-detect FSM (`startCalibration()` /
`CAL_WAITING_TOUCH` / `CAL_CAPTURING`, driven by `CAL_START` today). **Piezo
has no equivalent calibration routine at all** — right now piezo
thresholds/ceilings are only settable manually, one value at a time, via
`SET_THRESH`/`SET_CEILING_BASELINE`. Before this screen can do anything
for piezo, the drum sketch needs its own piezo calibration FSM built —
likely mirroring the velostat one's shape (rest phase to find noise floor,
then capture peak on a hard hit), but operating on `piezoThreshold`/
`piezoCeilingBaseline` instead of `veloRest`/`veloMax`. This is real new
drum-side feature work, not just protocol wiring — flagged here as a
blocking prerequisite for the piezo half of this screen.

**Protocol implications (needs a `UART_PROTOCOL.md` update, not decided
yet):** `CAL_START` currently takes no arguments and always runs a
full-all-pads velostat calibration. It needs to become parameterized —
something like `CAL_START,<sensor>,<scope>,<padIndex>` (padIndex only
meaningful for single-pad scopes) — and `CAL_STATE` messages need to
indicate which sensor type the state belongs to, so the UI can show the
right screen. This redesign should happen before implementing either the
piezo FSM or this screen's real wiring, not organically discovered while
building both at once.

**UI flow (once the above exists):**
1. Choose sensor type (Piezo / Velostat)
2. Choose scope (Single pad / Single pad → copy to all / All pads)
3. If a single-pad scope, pick the pad (reuse the hex layout component)
4. Start — screen mirrors the FSM's live state via `CAL_STATE` messages:
   "Resting — don't touch any pad" → "Touch pad to calibrate" → "Pad N
   calibrated" → "Complete" (or "Cancelled" for the `ABORTED` state)

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
2. Hex layout component (shared by landing page and Pad Assignment) —
   9-slot positions matching the physical shell, friendly 1-based numbering
   over 0-based internal indices, empty-slot styling for the 3 unpopulated
   positions
3. Landing page shell: hex layout + footer with all 4 icons (2 active, 2
   disabled/placeholder) — no serial logic yet, just layout and navigation
4. Pad Assignment screen — reuses the hex component, local UI + state only,
   `sendCommand()` stub for `SET_NOTE`
5. Tuning sub-menu shell (4 rows, navigation into each) — placeholders OK
   for all 4 sub-screens initially
6. Build out 4a–4c (sensitivity, curve editor, crosstalk) one at a time,
   local UI first, serial stub calls in the right places
7. **4d (Calibration) is blocked until the drum-side piezo calibration FSM
   exists and the `CAL_START`/`CAL_STATE` protocol is redesigned for
   sensor type + scope** (see section 4d above) — do NOT build this
   screen's real logic before that work happens on the drum side, though
   the sensor-type/scope *selection* UI (steps 1–3 of the flow) could
   reasonably be built now with everything past "Start" stubbed
8. Only after all screens exist and navigate correctly: implement the
   actual UART command sends for everything stubbed above, replacing the
   old throwaway `PAD,<label>` parsing entirely with real handling for
   `HIT`, `PADVAL`, `CAL_STATE`, `XTALKVAL`, `ACK`, `ERR`