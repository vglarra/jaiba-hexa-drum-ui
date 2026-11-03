# Hub ↔ ESP32 I2C Protocol

The Teensy 4.1 **hub** (`src/hub/main.cpp`, env `hub`) sits in the `U_UI`
socket and talks to the ESP32 7" UI over I2C. This file is the single source
of truth for the wire format; keep it in sync with the ESP32
`src/bridge_transport.cpp`.

## Physical link

| Signal | Teensy hub (U_UI) | ESP32 (display) |
|---|---|---|
| SDA | pin 18 | GPIO19 (display J6/J4) |
| SCL | pin 19 | GPIO20 |
| GND | GND | GND |
| 3.3 V | hub 3.3 V | display 3.3 V — **do not bridge** |
| Power | drum PCB | display 5 V / 2 A |

- Hub slave address: **0x42** (7-bit), 400 kHz.
- The same bus also carries the GT911 touch (0x5D) and the PCA9557 reset
  (0x18). Keep the wiring short, use the display's pull-ups, common ground.
- The Teensy 4.1 `Wire` buffer is 136 bytes; the ESP32 `Wire1` buffer is set
  to 128. The largest transfer is the 66-byte response frame / 60-byte knob
  config.

## Sensor UARTs

| Link | Hub pins | Sensor side |
|---|---|---|
| Right | `Serial1` RX 0 / TX 1 | U_R pins 0/1 |
| Left | `Serial3` RX 15 / TX 14 | U_L pins 0/1 (copper to U_UI 15/14) |

- 115200 8N1; one message per line; max 63 bytes.
- The hub forwards the raw line, **without parsing** it.
- Response frame (hub → ESP32): `[board][len][line]`, where `board` is 0
  (right) or 1 (left), `len` is 1–63, `line` is the raw sensor bytes. The
  `line` does not include the `\n`.
- Command frame (ESP32 → hub): `[len][line]`; the hub appends `\n` and writes
  the line to that board's UART.

## Register map

The ESP32 writes a register byte, then either reads a response or writes
data. A read uses a repeated start:
`beginTransmission(0x42); write(reg); endTransmission(false); requestFrom(...)`.

| Register | R/W | Size | Contents |
|---|---|---|---|
| `0x00` | R | 4 | `'J','K'`, version, flags |
| `0x04` | R | 8 | `u16` rightAvail, leftAvail, rightCmdSpace, leftCmdSpace (little-endian) |
| `0x0C` | R | 4 | knobCount, buttonCount, testMode, reserved |
| `0x10` | R | ≤66 | one response frame for the **right** board |
| `0x11` | R | ≤66 | one response frame for the **left** board |
| `0x20` | W | 1+n | command frame for the **right** board |
| `0x21` | W | 1+n | command frame for the **left** board |
| `0x30` | R | 12 | 6 × `u16` knob values (little-endian) |
| `0x40` | R/W | 60 | 6 × `KnobConfig` |
| `0x80` | R | 6 | 6 × `u8` button states |
| `0x88` | R/W | 24 | 6 × `ButtonConfig` |
| `0xA0` | W | 1 | write 1 = save config to EEPROM |
| `0xA1` | W | 1 | write 1/0 = test mode on/off |

- The status is the first read on every poll; only read `0x10`/`0x11` when
  the corresponding `available` count is non-zero. Each read pops one frame.
- If no frame is available, `0x10`/`0x11` return one zero byte.
- The response FIFO is 768 bytes per board; the command FIFO 128 bytes per
  board. The command space counts bytes free; the ESP32 backs off when small.
- `0x40`/`0x88` reads return the whole array in one transaction; writes
  replace the whole array first, then `0xA0` commits it to EEPROM.

## Config layouts

All little-endian, packed.

```c
struct KnobConfig {          // 10 bytes
    uint16_t controlId;
    uint8_t  cc;
    uint8_t  channel;
    uint16_t minValue;
    uint16_t maxValue;
    uint8_t  flags;          // bit0 enabled, bit1 14-bit
    uint8_t  reserved;
};

struct ButtonConfig {        // 4 bytes
    uint8_t note;            // note number, or CC number when mode == 1
    uint8_t channel;
    uint8_t mode;            // 0 = note on/off, 1 = CC momentary
    uint8_t flags;           // bit0 enabled
};
```

- A 14-bit knob sends CC `cc` (MSB) and CC `cc+32` (LSB) for one value.
- The knob value is scaled from the 12-bit ADC to `[minValue, maxValue]`.
- Defaults: knob 0 is enabled + 14-bit on A2/16, CC 20/52; knobs 1–5 are
  disabled (CC 7 and 22–25 reserved); buttons 0–5 are disabled.
- The ESP32 UI's fixed control table assigns `controlId`, CC, range and
  resolution; the hub only stores/sends them.

## Test mode

- `0xA1` or the USB serial `t` toggles it.
- While on, the hub injects a fake left `HIT,<pad>,<velocity>,P` roughly once
  per second into the left response FIFO. Use it to exercise the left Monitor
  while the real left sensor is absent.
- It does not touch the right stream or the real UARTs.

## USB-MIDI

- Product string: **`Jaiba Knobs`**; manufacturer `Jaiba`; USB type
  Serial + MIDI.
- Knobs send CC on change (threshold 2 for 7-bit, 32 for 14-bit).
- Buttons send note on/off or CC depending on `mode`.
- The sampler auto-detects this name; the hub is **not** in the audio path
  and the sensor boards keep their own USB-MIDI.

## Bench wiring (two Teensys)

- Right board: `U_R`, right sensor, `Jaiba Right`.
- Left board: `U_UI`, hub, `Jaiba Knobs`; `U_L` empty.
- ESP32 I2C: SDA 19 ↔ hub 18; SCL 20 ↔ hub 19; GND ↔ GND.
- Optional knob test: 10 kΩ pot, ends 3.3 V/GND, wiper A2/16.
- Hub USB serial (115200): `s` status, `t` test, `d` config.

## Notes / limits

- The hub is a single point for display monitoring and knob config, not for
  audio or sensor USB-MIDI. If it stalls, the drums still play.
- The status-first polling + short transactions are deliberate: the GT911
  touch shares the bus and is polled every 30 ms.
- The left sensor is absent in the two-Teensy bench; the test mode covers the
  UI left side.
- The knobs/buttons hardware and final pin map are still TBD; only knob 0 is
  enabled for the bench.
