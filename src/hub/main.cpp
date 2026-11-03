// Jaiba Hexa Drum — Teensy 4.1 control hub (U_UI socket)
//
// Responsibilities:
//   - Bridge the right sensor UART (Serial1, pins 0/1) and left sensor UART
//     (Serial3, pins 15/14) to the ESP32 over I2C. The bridge is transparent:
//     raw sensor lines are forwarded in [board][len][bytes] frames; the ESP32
//     keeps the protocol parser.
//   - Knobs/buttons -> USB-MIDI as "Jaiba Knobs".
//   - I2C slave at 0x42 with a small register/FIFO map (see the ESP32
//     BridgeTransport and Docs/CONTROL_HUB_ESP32_PLAN.md).
//   - Knob/button config in EEPROM, so assignments survive power cycles.
//   - Test mode: inject fake left-sensor HIT lines while the left board is
//     absent during the two-Teensy bench setup.
//
// Build/flash:  pio run -e hub -t upload --upload-port /dev/ttyACM1
// The board must be in the U_UI socket for the sensor UARTs to reach it.

#include <Arduino.h>
#include <EEPROM.h>
#include <Wire.h>
#include <usb_names.h>

// ============================================================
// USB identity — the sampler auto-detects this fixed product string.
// ============================================================
#define JAIBA_USB_PRODUCT {'J', 'a', 'i', 'b', 'a', ' ', 'K', 'n', 'o', 'b', 's'}
#define JAIBA_USB_PRODUCT_LEN 11

extern "C" {
struct usb_string_descriptor_struct usb_string_manufacturer_name = {
    2 + 5 * 2, 3, {'J', 'a', 'i', 'b', 'a'}
};
struct usb_string_descriptor_struct usb_string_product_name = {
    2 + JAIBA_USB_PRODUCT_LEN * 2, 3, JAIBA_USB_PRODUCT
};
}

namespace {

// ============================================================
// I2C protocol constants (must match the ESP32 BridgeTransport)
// ============================================================
constexpr uint8_t kI2cAddress = 0x42;

constexpr uint8_t kBoardRight = 0;
constexpr uint8_t kBoardLeft = 1;

constexpr size_t kLineMax = 63;          // sensor protocol line limit
constexpr size_t kFrameMax = kLineMax + 2; // [board][len][line]
constexpr size_t kResponseCap = 768;     // per board
constexpr size_t kCommandCap = 128;      // per board

constexpr uint8_t kKnobCount = 6;
constexpr uint8_t kButtonCount = 6;
constexpr uint16_t kConfigMagic = 0x4A4B;
constexpr uint8_t kConfigVersion = 1;

// Registers
constexpr uint8_t kRegId = 0x00;        // 'J','K', version, flags
constexpr uint8_t kRegStatus = 0x04;    // 4 x u16: rAvail, lAvail, rCmdSpace, lCmdSpace
constexpr uint8_t kRegCounts = 0x0C;    // knobCount, buttonCount, testMode, reserved
constexpr uint8_t kRegRespRight = 0x10; // read one response frame
constexpr uint8_t kRegRespLeft = 0x11;
constexpr uint8_t kRegCmdRight = 0x20;  // write [len][bytes]
constexpr uint8_t kRegCmdLeft = 0x21;
constexpr uint8_t kRegKnobValues = 0x30; // 6 x u16
constexpr uint8_t kRegKnobConfig = 0x40; // 6 x KnobConfig
constexpr uint8_t kRegButtonStates = 0x80; // 6 x u8
constexpr uint8_t kRegButtonConfig = 0x88; // 6 x ButtonConfig
constexpr uint8_t kRegCommit = 0xA0;
constexpr uint8_t kRegTestMode = 0xA1;

// ============================================================
// Config
// ============================================================
struct KnobConfig {
    uint16_t controlId;
    uint8_t cc;
    uint8_t channel;
    uint16_t minValue;
    uint16_t maxValue;
    uint8_t flags;     // bit0 enabled, bit1 14-bit
    uint8_t reserved;
} __attribute__((packed));

struct ButtonConfig {
    uint8_t note;      // note number, or CC number when mode == 1
    uint8_t channel;
    uint8_t mode;      // 0 = note on/off, 1 = CC momentary
    uint8_t flags;     // bit0 enabled
} __attribute__((packed));

struct HubConfig {
    uint16_t magic;
    uint8_t version;
    uint8_t knobCount;
    uint8_t buttonCount;
    uint8_t reserved;
    KnobConfig knobs[kKnobCount];
    ButtonConfig buttons[kButtonCount];
} __attribute__((packed));

HubConfig config;

// Pin plan for the U_UI socket. These are placeholders until the knob/button
// hardware is defined: only knob 0 (A2) is enabled by default.
const uint8_t kKnobPins[kKnobCount] = {16, 17, 20, 21, 22, 23}; // A2,A3,A6,A7,A8,A9
const uint8_t kButtonPins[kButtonCount] = {2, 3, 4, 5, 6, 7};

uint16_t knobValues[kKnobCount] = {};
uint16_t knobLastSent[kKnobCount] = {0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF};
bool buttonLast[kButtonCount] = {};
uint8_t buttonStates[kButtonCount] = {};

volatile uint8_t currentRegister = 0;
volatile bool saveRequested = false;
volatile uint8_t testMode = 0;

unsigned long lastFakeMs = 0;
uint8_t fakeIndex = 0;

// ============================================================
// Ring buffers (single producer / single consumer)
// ============================================================
template <size_t N>
struct Ring {
    uint8_t data[N];
    volatile size_t head = 0;
    volatile size_t used = 0;

    size_t freeSpace() const { return N - used; }

    bool push(const uint8_t* src, size_t len) {
        if (len > N - used) return false;
        for (size_t i = 0; i < len; i++) {
            data[(head + used + i) % N] = src[i];
        }
        used += len;
        return true;
    }

    size_t peek(uint8_t* dst, size_t maxLen) const {
        size_t n = used < maxLen ? used : maxLen;
        for (size_t i = 0; i < n; i++) {
            dst[i] = data[(head + i) % N];
        }
        return n;
    }

    uint8_t peekByte(size_t offset) const {
        return data[(head + offset) % N];
    }

    void drop(size_t len) {
        if (len > used) len = used;
        head = (head + len) % N;
        used -= len;
    }
};

// Response frames are [board][len][line bytes].
Ring<kResponseCap> responseRing[2];
// Command frames are [len][line bytes]; the main loop adds the trailing '\n'.
Ring<kCommandCap> commandRing[2];

char lineBuffer[2][kLineMax + 1];
uint8_t lineLength[2] = {};

// ============================================================
// Config persistence
// ============================================================
void setDefaultConfig() {
    memset(&config, 0, sizeof(config));
    config.magic = kConfigMagic;
    config.version = kConfigVersion;
    config.knobCount = kKnobCount;
    config.buttonCount = kButtonCount;

    for (uint8_t i = 0; i < kKnobCount; i++) {
        config.knobs[i].controlId = i;
        config.knobs[i].cc = (i == 1) ? 7 : (20 + i);
        config.knobs[i].channel = 1;
        config.knobs[i].minValue = 0;
        config.knobs[i].maxValue = (i == 0) ? 16383 : 127;
        // Knob 0 is the bench-test pot: enabled + 14-bit (CC 20 / 52).
        config.knobs[i].flags = (i == 0) ? 0x03 : 0x00;
        config.knobs[i].reserved = 0;
    }

    for (uint8_t i = 0; i < kButtonCount; i++) {
        config.buttons[i].note = 0;
        config.buttons[i].channel = 1;
        config.buttons[i].mode = 0;
        config.buttons[i].flags = 0; // disabled until the hardware is defined
    }
}

void loadConfig() {
    EEPROM.get(0, config);
    if (config.magic != kConfigMagic ||
        config.version != kConfigVersion ||
        config.knobCount != kKnobCount ||
        config.buttonCount != kButtonCount) {
        setDefaultConfig();
        EEPROM.put(0, config);
    }
}

void saveConfig() {
    HubConfig copy;
    noInterrupts();
    copy = config;
    saveRequested = false;
    interrupts();
    copy.magic = kConfigMagic;
    copy.version = kConfigVersion;
    EEPROM.put(0, copy);
}

// ============================================================
// Bridge rings
// ============================================================
void pushResponse(uint8_t board, const char* line) {
    size_t len = strlen(line);
    if (len > kLineMax) len = kLineMax;

    uint8_t frame[kFrameMax];
    frame[0] = board;
    frame[1] = (uint8_t)len;
    memcpy(frame + 2, line, len);

    noInterrupts();
    bool ok = responseRing[board].push(frame, len + 2);
    interrupts();

    if (!ok) {
        Serial.print("response ring full, dropped: ");
        Serial.println(line);
    }
}

void pushCommand(uint8_t board, const uint8_t* bytes, size_t len) {
    if (len == 0 || len > kLineMax) return;
    uint8_t frame[kLineMax + 1];
    frame[0] = (uint8_t)len;
    memcpy(frame + 1, bytes, len);
    commandRing[board].push(frame, len + 1);
}

void readUart(HardwareSerial& port, uint8_t board) {
    while (port.available()) {
        int c = port.read();
        if (c < 0) break;

        if (c == '\n') {
            lineBuffer[board][lineLength[board]] = '\0';
            while (lineLength[board] > 0 &&
                   (lineBuffer[board][lineLength[board] - 1] == '\r' ||
                    lineBuffer[board][lineLength[board] - 1] == ' ')) {
                lineBuffer[board][--lineLength[board]] = '\0';
            }
            if (lineLength[board] > 0) {
                pushResponse(board, lineBuffer[board]);
            }
            lineLength[board] = 0;
        } else if (lineLength[board] < kLineMax) {
            lineBuffer[board][lineLength[board]++] = (char)c;
        } else {
            // Runaway/garbled line: drop it.
            lineLength[board] = 0;
        }
    }
}

void drainCommands(uint8_t board) {
    HardwareSerial& port = (board == kBoardRight) ? Serial1 : Serial3;

    for (;;) {
        uint8_t frame[kLineMax + 1];
        size_t len = 0;

        noInterrupts();
        if (commandRing[board].used < 1) {
            interrupts();
            break;
        }
        uint8_t payloadLen = commandRing[board].peekByte(0);
        if (payloadLen == 0 || payloadLen > kLineMax ||
            commandRing[board].used < (size_t)payloadLen + 1) {
            interrupts();
            break;
        }
        len = commandRing[board].peek(frame, payloadLen + 1);
        commandRing[board].drop(payloadLen + 1);
        interrupts();

        if (len > 1) {
            port.write(frame + 1, len - 1);
            port.write('\n');
        }
    }
}

// ============================================================
// I2C slave
// ============================================================
void buildStatus(uint8_t* out) {
    uint16_t rAvail = responseRing[kBoardRight].used;
    uint16_t lAvail = responseRing[kBoardLeft].used;
    uint16_t rSpace = commandRing[kBoardRight].freeSpace();
    uint16_t lSpace = commandRing[kBoardLeft].freeSpace();

    out[0] = rAvail & 0xFF;
    out[1] = rAvail >> 8;
    out[2] = lAvail & 0xFF;
    out[3] = lAvail >> 8;
    out[4] = rSpace & 0xFF;
    out[5] = rSpace >> 8;
    out[6] = lSpace & 0xFF;
    out[7] = lSpace >> 8;
}

void sendResponse(uint8_t board) {
    uint8_t frame[kFrameMax];
    size_t n = 0;

    // Runs in the I2C ISR; the main loop disables this interrupt around its
    // ring operations, so the ring state is stable here.
    if (responseRing[board].used >= 2) {
        uint8_t payloadLen = responseRing[board].peekByte(1);
        size_t total = (size_t)payloadLen + 2;
        if (responseRing[board].used >= total) {
            n = responseRing[board].peek(frame, total);
            responseRing[board].drop(total);
        }
    }

    if (n == 0) {
        uint8_t zero = 0;
        Wire.write(&zero, 1);
    } else {
        Wire.write(frame, n);
    }
}

void onI2cReceive(int count) {
    if (count < 1 || !Wire.available()) return;

    uint8_t reg = Wire.read();
    currentRegister = reg;

    switch (reg) {
        case kRegCmdRight:
        case kRegCmdLeft: {
            if (!Wire.available()) return;
            uint8_t len = Wire.read();
            uint8_t board = (reg == kRegCmdRight) ? kBoardRight : kBoardLeft;
            uint8_t bytes[kLineMax];
            size_t n = 0;
            while (Wire.available() && n < len && n < kLineMax) {
                bytes[n++] = Wire.read();
            }
            if (n > 0) pushCommand(board, bytes, n);
            break;
        }

        case kRegKnobConfig: {
            uint8_t* dst = reinterpret_cast<uint8_t*>(&config.knobs);
            size_t n = sizeof(config.knobs);
            for (size_t i = 0; i < n && Wire.available(); i++) {
                dst[i] = Wire.read();
            }
            break;
        }

        case kRegButtonConfig: {
            uint8_t* dst = reinterpret_cast<uint8_t*>(&config.buttons);
            size_t n = sizeof(config.buttons);
            for (size_t i = 0; i < n && Wire.available(); i++) {
                dst[i] = Wire.read();
            }
            break;
        }

        case kRegCommit:
            if (Wire.available()) {
                uint8_t v = Wire.read();
                if (v) saveRequested = true;
            }
            break;

        case kRegTestMode:
            if (Wire.available()) {
                uint8_t v = Wire.read();
                testMode = v ? 1 : 0;
                lastFakeMs = 0;
            }
            break;

        default:
            break;
    }
}

void onI2cRequest() {
    switch (currentRegister) {
        case kRegId: {
            uint8_t out[4] = {'J', 'K', kConfigVersion, 0};
            Wire.write(out, sizeof(out));
            break;
        }
        case kRegStatus: {
            uint8_t out[8];
            buildStatus(out);
            Wire.write(out, sizeof(out));
            break;
        }
        case kRegCounts: {
            uint8_t out[4] = {kKnobCount, kButtonCount, testMode, 0};
            Wire.write(out, sizeof(out));
            break;
        }
        case kRegRespRight:
            sendResponse(kBoardRight);
            break;
        case kRegRespLeft:
            sendResponse(kBoardLeft);
            break;
        case kRegKnobValues: {
            uint8_t out[kKnobCount * 2];
            for (uint8_t i = 0; i < kKnobCount; i++) {
                out[i * 2] = knobValues[i] & 0xFF;
                out[i * 2 + 1] = knobValues[i] >> 8;
            }
            Wire.write(out, sizeof(out));
            break;
        }
        case kRegKnobConfig:
            Wire.write(reinterpret_cast<const uint8_t*>(&config.knobs), sizeof(config.knobs));
            break;
        case kRegButtonStates:
            Wire.write(buttonStates, sizeof(buttonStates));
            break;
        case kRegButtonConfig:
            Wire.write(reinterpret_cast<const uint8_t*>(&config.buttons), sizeof(config.buttons));
            break;
        default: {
            uint8_t zero = 0;
            Wire.write(&zero, 1);
            break;
        }
    }
}

// ============================================================
// Knobs and buttons
// ============================================================
void updateKnobs() {
    for (uint8_t i = 0; i < kKnobCount; i++) {
        if (!(config.knobs[i].flags & 0x01)) continue;

        uint16_t raw = analogRead(kKnobPins[i]);
        uint16_t lo = config.knobs[i].minValue;
        uint16_t hi = config.knobs[i].maxValue;
        if (hi < lo) {
            uint16_t t = lo;
            lo = hi;
            hi = t;
        }

        uint16_t value = lo;
        if (hi > lo) {
            value = lo + (uint32_t)raw * (hi - lo) / 4095;
        }
        knobValues[i] = value;

        bool fourteenBit = (config.knobs[i].flags & 0x02) != 0;
        uint16_t threshold = fourteenBit ? 32 : 2;
        int delta = (int)value - (int)knobLastSent[i];
        if (delta < 0) delta = -delta;
        if (delta < threshold) continue;

        uint8_t cc = config.knobs[i].cc;
        uint8_t channel = config.knobs[i].channel;
        if (fourteenBit) {
            usbMIDI.sendControlChange(cc, value >> 7, channel);
            usbMIDI.sendControlChange(cc + 32, value & 0x7F, channel);
        } else {
            usbMIDI.sendControlChange(cc, value & 0x7F, channel);
        }
        knobLastSent[i] = value;
    }
}

void updateButtons() {
    for (uint8_t i = 0; i < kButtonCount; i++) {
        if (!(config.buttons[i].flags & 0x01)) continue;

        bool pressed = !digitalRead(kButtonPins[i]);
        buttonStates[i] = pressed ? 1 : 0;
        if (pressed == buttonLast[i]) continue;
        buttonLast[i] = pressed;

        uint8_t channel = config.buttons[i].channel;
        if (config.buttons[i].mode == 0) {
            if (pressed) {
                usbMIDI.sendNoteOn(config.buttons[i].note, 127, channel);
            } else {
                usbMIDI.sendNoteOff(config.buttons[i].note, 0, channel);
            }
        } else {
            usbMIDI.sendControlChange(config.buttons[i].note, pressed ? 127 : 0, channel);
        }
    }
}

// ============================================================
// Test mode — fake left sensor while the left board is absent
// ============================================================
void updateTestMode() {
    if (!testMode) return;
    if (lastFakeMs != 0 && millis() - lastFakeMs < 900) return;

    lastFakeMs = millis();
    char line[24];
    uint8_t pad = fakeIndex % 7;
    uint8_t velocity = 40 + ((fakeIndex * 13) % 80);
    snprintf(line, sizeof(line), "HIT,%u,%u,P", pad, velocity);
    pushResponse(kBoardLeft, line);
    fakeIndex++;
}

// ============================================================
// USB serial debug
// ============================================================
void printStatus() {
    Serial.printf("hub: I2C 0x%02X, test=%u, R avail=%u, L avail=%u\n",
                  kI2cAddress, testMode,
                  (unsigned)responseRing[kBoardRight].used,
                  (unsigned)responseRing[kBoardLeft].used);
    for (uint8_t i = 0; i < kKnobCount; i++) {
        Serial.printf("knob %u: id=%u cc=%u ch=%u min=%u max=%u flags=0x%02X value=%u\n",
                      i, config.knobs[i].controlId, config.knobs[i].cc,
                      config.knobs[i].channel, config.knobs[i].minValue,
                      config.knobs[i].maxValue, config.knobs[i].flags,
                      knobValues[i]);
    }
}

void handleSerialInput() {
    static String line;

    while (Serial.available()) {
        char c = (char)Serial.read();
        if (c == '\n') {
            line.trim();
            if (line == "t") {
                testMode = testMode ? 0 : 1;
                lastFakeMs = 0;
                Serial.printf("test mode: %u\n", testMode);
            } else if (line == "s") {
                printStatus();
            } else if (line == "d") {
                for (uint8_t i = 0; i < kKnobCount; i++) {
                    Serial.printf("knob %u id=%u cc=%u\n", i,
                                  config.knobs[i].controlId,
                                  config.knobs[i].cc);
                }
            }
            line = "";
        } else {
            line += c;
            if (line.length() > 32) line = "";
        }
    }
}

} // namespace

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println();
    Serial.println("Jaiba Knobs hub — Teensy 4.1");

    Serial1.begin(115200);
    Serial3.begin(115200);
    analogReadResolution(12);

    for (uint8_t i = 0; i < kButtonCount; i++) {
        pinMode(kButtonPins[i], INPUT_PULLUP);
        buttonLast[i] = false;
        buttonStates[i] = 0;
    }

    loadConfig();

    Wire.begin(kI2cAddress);
    Wire.onReceive(onI2cReceive);
    Wire.onRequest(onI2cRequest);

    Serial.printf("I2C slave 0x%02X — Serial1=right, Serial3=left\n", kI2cAddress);
    Serial.printf("Knobs=%u Buttons=%u — knob 0 test pot on A2/16\n",
                  kKnobCount, kButtonCount);
    Serial.println("USB MIDI: Jaiba Knobs");
    Serial.println("Commands: t = test mode, s = status, d = config");
}

void loop() {
    readUart(Serial1, kBoardRight);
    readUart(Serial3, kBoardLeft);

    drainCommands(kBoardRight);
    drainCommands(kBoardLeft);

    updateKnobs();
    updateButtons();
    updateTestMode();
    handleSerialInput();

    if (saveRequested) {
        saveConfig();
        Serial.println("config saved to EEPROM");
    }

    // Drain any incoming USB-MIDI so the endpoint does not fill up.
    while (usbMIDI.read()) {
    }

    delay(1);
}
