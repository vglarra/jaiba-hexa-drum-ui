# TFT Touch UI — Project Guide for Claude Code

Separate project from the DIVA drum MIDI controller. This will eventually
become a touchscreen UI (likely for controlling/monitoring the drum project,
or another purpose TBD), but **phase 1 is hardware bring-up only** — confirm
the display and touch controller both work correctly before any UI design
work starts.

## Current phase: hardware verification

Do not start UI/LVGL work yet. The immediate goal is just: does the display
draw correctly, and does touch report accurate coordinates. Once both are
confirmed working reliably, UI design becomes a new, separate effort.

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

`src/main.cpp` — polls the XPT2046 manually over SPI (no touch library),
draws a red dot at the touched location, and prints raw + mapped
coordinates to Serial. This is deliberately simple: the goal right now is
only to confirm hardware works, not to build any real UI logic yet.

Calibration constants (found by touching the four physical screen corners
and reading raw values from Serial Monitor):
```cpp
#define CALIBRATION_X_MIN 1840
#define CALIBRATION_X_MAX 31480
#define CALIBRATION_Y_MIN 31104
#define CALIBRATION_Y_MAX 1824
```
If the display is ever swapped, or touch coordinates seem inverted/off,
these need to be re-derived — don't assume they transfer to different
hardware.

## What "working" looks like for phase 1

1. Upload `main.cpp` — screen should show black background with white text:
   "Touch Screen Test" / "Check Serial Monitor"
2. Touch any point on screen — a red dot should appear at that location
3. Serial Monitor should print raw X/Y and mapped X/Y for each touch —
   double check the configured baud rate in `main.cpp` matches what's set
   in the Serial Monitor before assuming a "no output" failure is real
4. Touch the four corners — mapped coordinates should land close to the
   screen's actual corners, i.e. touch position visually matches where the
   red dot is drawn

If any of this doesn't work, the priority is debugging hardware/wiring/
config — not moving on to UI work.

## Explicitly NOT started yet

- No LVGL integration (an earlier exploratory version using LVGL exists in
  project history/notes, but that was set aside in favor of getting basic
  TFT_eSPI + manual touch working first — LVGL work has not resumed)
- No UI layout, screens, widgets, or navigation design
- No decision yet on what this UI actually controls/displays (could be
  drum project monitoring, could be something else — undecided)

## Next steps (after hardware verification passes)

- Confirm touch calibration is stable/repeatable across multiple test runs
- Only then: begin a genuinely separate UI design phase — likely worth a
  fresh planning discussion on layout/purpose before writing any UI code