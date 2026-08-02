# TFT Touch UI — Project Guide for Claude Code

Separate project from the DIVA drum MIDI controller. This will eventually
become a touchscreen UI for controlling/monitoring the drum project.
**Phase 1 (hardware bring-up) is complete** — display and touch are both
confirmed working, including a fix for resistive-touch jitter via
oversampling/averaging. The UART link to the drum Teensy has also been
tested end-to-end and confirmed working.

## Current phase: UART protocol design

With hardware verified, the current focus is designing the real UART
message protocol between the drum Teensy and this board. This protocol is
a prerequisite for building the Pad Assignment and Tuning screens described
in UI_PLAN.md — those screens can't be implemented against a placeholder
message format. Do not start on those screens until the protocol is
designed.

## Milestones

- **Phase 1 (hardware verification) — complete.** Display draws correctly
  and touch reports accurate, stable coordinates. Resistive touch was
  initially jittery; fixed by oversampling and averaging multiple raw
  reads per touch event before mapping to screen coordinates.
- **UART link to drum Teensy — tested end-to-end, confirmed working.**
  Test setup: the drum Teensy sends `PAD,<label>\n` on Serial1 whenever a
  pad is hit; this board reads the line and displays the pad label large
  and centered on screen. Verified reliable over repeated hits.
- **`src/main.cpp` is currently throwaway UART test code, not production
  code.** It uses a placeholder message format (`PAD,<label>`), not the
  real protocol that will come out of the current design phase. It also
  temporarily replaced the previous touch-test screen (the red-dot +
  Serial coordinate output described below) — that code is not deleted,
  just superseded; it's recoverable from git history if needed again.

## Hardware

- **MCU:** Teensy 4.1
- **Display:** ST7796 driver, 320×480 resolution
- **Touch:** XPT2046 resistive touch controller, on the same physical board
  as the display (combo module, board silkscreen reads "X320 V1.1")
- Display and touch share one SPI bus; only CS lines (and IRQ) are unique
  per chip

## Pin mapping (confirmed against board photo)

| Board pin | Teensy pin | Shared with |
|---|---|---|
| VCC | 3.3V | — |
| GND | GND | — |
| SCK | 13 | T_CLK |
| SDI (MOSI) | 11 | T_DIN |
| SDO (MISO) | 12 | T_DO |
| CS | 10 | — (display CS, unique) |
| DC/RS | 8 | — |
| RESET | 9 | — |
| LED | 3.3V | Backlight, always-on |
| T_CS | 6 | — (touch CS, unique) |
| T_CLK | 13 | SCK |
| T_DIN | 11 | SDI (MOSI) |
| T_DO | 12 | SDO (MISO) |
| T_IRQ | 7 | — (not used yet; current code polls instead of using interrupts) |

**Do not use 5V anywhere** — this module is 3.3V logic throughout.

## Software setup

- **Toolchain:** PlatformIO in VS Code (not Arduino IDE)
- **Library:** `bodmer/TFT_eSPI@^2.5.43`
- **Configuration approach:** `TFT_eSPI` is configured entirely via
  `build_flags` in `platformio.ini` (compile-time defines), not by editing
  the library's `User_Setup.h` directly — keeps config in version control
  and avoids fragile edits inside `.pio`'s cached library copy.
- `platform = teensy` in `platformio.ini` — if the build fails with a
  "platform not found" error, check whether the installed PlatformIO
  version expects `teensysduino` instead; this has shifted between
  PlatformIO releases.

## Current test code

`src/main.cpp` currently holds the UART link test (see Milestones above),
not the touch test. As noted there, this is throwaway code with a
placeholder protocol — treat it as disposable once real protocol work
starts, not as something to extend.

The touch-test code (polls the XPT2046 manually over SPI, draws a red dot
at the touched location, prints raw + mapped coordinates to Serial) is no
longer in `main.cpp` but is recoverable from git history. Its calibration
constants, found by touching the four physical screen corners and reading
raw values from Serial Monitor, are still the confirmed-good values for
this hardware:
```cpp
#define CALIBRATION_X_MIN 1840
#define CALIBRATION_X_MAX 31480
#define CALIBRATION_Y_MIN 31104
#define CALIBRATION_Y_MAX 1824
```
If the display is ever swapped, or touch coordinates seem inverted/off,
these need to be re-derived — don't assume they transfer to different
hardware.

## Phase 1 verification (complete, kept for reference)

This is what "working" was confirmed to look like when the touch-test
code was `main.cpp`; useful if that code is ever restored to re-verify
hardware after a change:

1. Upload the touch-test `main.cpp` — screen shows black background with
   white text: "Touch Screen Test" / "Check Serial Monitor"
2. Touch any point on screen — a red dot appears at that location
3. Serial Monitor prints raw X/Y and mapped X/Y for each touch — double
   check the configured baud rate in `main.cpp` matches what's set in the
   Serial Monitor before assuming a "no output" failure is real
4. Touch the four corners — mapped coordinates land close to the screen's
   actual corners, i.e. touch position visually matches where the red dot
   is drawn

## Explicitly NOT started yet

- The real UART message protocol (currently in design — see Current
  phase above); `src/main.cpp`'s `PAD,<label>` format is a placeholder
- Pad Assignment and Tuning screens (described in UI_PLAN.md) — blocked
  on the protocol design above
- No LVGL integration (an earlier exploratory version using LVGL exists in
  project history/notes, but that was set aside in favor of getting basic
  TFT_eSPI + manual touch working first — LVGL work has not resumed)
- No broader UI layout, navigation, or screens beyond Pad Assignment/Tuning
  have been designed yet

## Next steps

- Design the real UART message protocol between the drum Teensy and this
  board (current focus)
- Implement the Pad Assignment and Tuning screens per UI_PLAN.md against
  the finalized protocol
- Revisit touch-test code from git history if hardware changes and
  recalibration/re-verification is needed