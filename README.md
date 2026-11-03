# Jaiba Hexa Drum — Teensy UI / Control Hub

This repo now has two Teensy 4.1 roles:

| Environment | Role |
|---|---|
| `hub` | **Control hub in the `U_UI` socket** — sensor UART bridge, knobs/buttons, USB-MIDI `Jaiba Knobs`, I2C slave 0x42, EEPROM |
| `teensy41` | Legacy ST7796/XPT2046 TFT UI — reference only; the ESP32 7" UI replaces it |

The ESP32 UI lives in
[`jaiba-hexa-drum-esp32-ui`](https://github.com/vglarra/jaiba-hexa-drum-esp32-ui);
the sensor firmware lives in
[`jaiba-hexa-drum-sensor-code`](https://github.com/vglarra/jaiba-hexa-drum-sensor-code).

## Hub hardware (U_UI socket)

| Function | Teensy 4.1 pins |
|---|---|
| Right sensor UART | `Serial1` — RX 0, TX 1 |
| Left sensor UART | `Serial3` — RX 15, TX 14 |
| ESP32 I2C slave | `Wire` — SDA 18, SCL 19; address **0x42**, 400 kHz |
| Knob test pot | A2 / pin 16 (other 5 knob slots reserved) |
| Button slots | pins 2–7 (disabled until the hardware is defined) |
| USB | `Jaiba Knobs` USB-MIDI + serial |

- **Serial1** receives the right sensor's lines; **Serial3** receives the left
  sensor's lines (U_L copper runs to U_UI 15/14). The hub does not parse the
  sensor protocol; it forwards raw lines in `[board][len][bytes]` frames.
- **I2C** carries the sensor frames to the ESP32, the knob/button config and
  the knob values. The ESP32's display touch (GT911) shares that bus, so the
  hub uses a status-first, short-transaction design.
- **EEPROM** stores the knob/button config; it is loaded at boot and saved
  when the ESP32 writes the commit register.
- **Test mode** injects fake left-sensor `HIT` lines for the two-Teensy bench
  (left sensor absent). Toggle it over I2C (`0xA1`) or the USB serial `t`.

Full register map, frame format and wiring:
[`HUB_PROTOCOL.md`](HUB_PROTOCOL.md).

## Build and flash

```bash
# Hub (left Teensy in U_UI during the bench)
pio run -e hub
pio run -e hub -t upload --upload-port /dev/ttyACM1

# Legacy TFT UI (reference only)
pio run -e teensy41
pio run -e teensy41 -t upload
```

> The Teensy uploader auto-searches when more than one Teensy is connected.
> Check `/dev/serial/by-id/` before flashing: the hub should enumerate as
> `Jaiba Knobs`. If it does not, fix the port selection before retrying.

USB serial (115200): `s` prints status/knob values, `t` toggles test mode,
`d` dumps the knob control IDs.

## Two-Teensy bench

- Right board: `U_R`, right sensor firmware, USB `Jaiba Right`.
- Left board: `U_UI`, hub firmware, USB `Jaiba Knobs`. `U_L` stays empty.
- ESP32 display I2C: SDA 19 → hub 18, SCL 20 → hub 19, GND ↔ GND; display
  powered from its own 5 V/2 A; do not bridge the 3.3 V rails.
- The sampler sees `Jaiba Right` + `Jaiba Knobs`; `Jaiba Left` is absent.
  The third sampler input is the next phase.

## Legacy TFT UI

`src/main.cpp`, `screens.*`, `hex_grid.*`, `uart_protocol.*` and
`drum_state.*` are the old ST7796/XPT2046 UI. They are kept for reference and
excluded from the hub build by `build_src_filter`. The ESP32 UI implements the
new Monitor/Knobs screens; do not add new UI work here.
