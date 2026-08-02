# Jaiba Hexa Drum UI

Touchscreen UI firmware for a Teensy 4.1, built with PlatformIO. This is a
separate project from the Jaiba drum MIDI controller — it will eventually
become a touchscreen UI (likely for controlling/monitoring the drum
project, or another purpose TBD), but **phase 1 is hardware bring-up
only**: confirming the display and touch controller both work correctly
before any UI design work starts.

## Hardware

- **MCU:** Teensy 4.1
- **Display:** ST7796 driver, 320×480, SPI
- **Touch:** XPT2046 resistive touch controller, on the same combo module
  as the display (board silkscreen "X320 V1.1"), sharing the display's SPI
  bus with its own CS line

## Status

Phase 1 (display + touch verification) is working: the ST7796 renders
correctly, and touch input is read via manual XPT2046 SPI polling
(median-of-7 oversampling for jitter reduction), calibrated against the
physical screen corners.

UI design (LVGL or otherwise) has not started yet — see `Claude.md` for
full hardware/pin details and current project state.

## Build

```
pio run              # build
pio run -t upload    # flash to Teensy 4.1
```

Configuration (display driver, pins, fonts) lives entirely in
`platformio.ini` build flags — no edits to the `TFT_eSPI` library's
`User_Setup.h` are needed.
