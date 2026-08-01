#include <Arduino.h>
#include <TFT_eSPI.h>

// ============================================================
// TFT + TOUCH TEST — Teensy 4.1, ST7796 display, XPT2046 touch
//
// Display and touch pin config lives in platformio.ini (build_flags),
// not here — TFT_eSPI reads its setup at compile time via those defines.
//
// Wiring reference (from hardware research):
//   TFT (ST7796):   MISO=12  MOSI=11  SCLK=13  CS=10  DC=8  RST=9  LED=3.3V
//   Touch (XPT2046): shares TFT's SPI bus, CS=6, IRQ=7 (unused, polling only)
//
// Calibration values below were found by touching the four corners of the
// physical screen and reading the raw X/Y from Serial Monitor. Re-run that
// process if you ever swap displays or the readings drift.
// ============================================================

TFT_eSPI tft = TFT_eSPI();

#define CALIBRATION_X_MIN 1840
#define CALIBRATION_X_MAX 31480
#define CALIBRATION_Y_MIN 31104
#define CALIBRATION_Y_MAX 1824

bool readTouchData(uint16_t &x, uint16_t &y);
uint16_t medianOfSamples(uint16_t *samples, uint8_t count);

// Oversampling settings for readTouchData(): resistive touch ADC reads are
// noisy on a single sample, so we take a small burst per touch and use the
// median (robust to a single spurious glitch) instead of the raw value.
// 7 samples * ~500us settling between them adds ~3ms of latency total —
// well under perceptible touch lag, unlike the longer settling times we
// had to back off from on the piezo/drum project.
#define TOUCH_SAMPLES 7
#define TOUCH_SETTLE_US 500

void setup() {
    Serial.begin(9600);
    while (!Serial && millis() < 2000); // don't hang forever if no monitor is attached

    tft.init();
    tft.setRotation(0);
    tft.fillScreen(TFT_BLACK);

    tft.setCursor(10, 10);
    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(2);
    tft.println("Touch Screen Test");
    tft.println("Check Serial Monitor");

    pinMode(TOUCH_CS, OUTPUT);
    digitalWrite(TOUCH_CS, HIGH); // deselected by default
}

void loop() {
    uint16_t x_raw, y_raw;
    if (readTouchData(x_raw, y_raw)) {
        Serial.print("Raw X: "); Serial.print(x_raw);
        Serial.print(", Raw Y: "); Serial.println(y_raw);

        uint16_t mapped_x = map(x_raw, CALIBRATION_X_MIN, CALIBRATION_X_MAX, 0, tft.width());
        uint16_t mapped_y = map(y_raw, CALIBRATION_Y_MIN, CALIBRATION_Y_MAX, 0, tft.height());

        Serial.print("Mapped X: "); Serial.print(mapped_x);
        Serial.print(", Mapped Y: "); Serial.println(mapped_y);

        tft.fillCircle(mapped_x, mapped_y, 5, TFT_RED);
    }

    delay(100);
}

bool readTouchData(uint16_t &x, uint16_t &y) {
    uint16_t xSamples[TOUCH_SAMPLES];
    uint16_t ySamples[TOUCH_SAMPLES];

    digitalWrite(TOUCH_CS, LOW);
    SPI.beginTransaction(SPISettings(2500000, MSBFIRST, SPI_MODE0));

    for (uint8_t i = 0; i < TOUCH_SAMPLES; i++) {
        SPI.transfer(0x90);              // command: read Y
        ySamples[i] = SPI.transfer16(0x0000);
        SPI.transfer(0xD0);              // command: read X
        xSamples[i] = SPI.transfer16(0x0000);
        if (i < TOUCH_SAMPLES - 1) delayMicroseconds(TOUCH_SETTLE_US);
    }

    SPI.endTransaction();
    digitalWrite(TOUCH_CS, HIGH);

    x = medianOfSamples(xSamples, TOUCH_SAMPLES);
    y = medianOfSamples(ySamples, TOUCH_SAMPLES);

    if (x > 100 && y > 100) {
        return true;
    }
    return false;
}

uint16_t medianOfSamples(uint16_t *samples, uint8_t count) {
    // Insertion sort — count is small (single-digit), so this is cheap.
    for (uint8_t i = 1; i < count; i++) {
        uint16_t key = samples[i];
        int8_t j = i - 1;
        while (j >= 0 && samples[j] > key) {
            samples[j + 1] = samples[j];
            j--;
        }
        samples[j + 1] = key;
    }
    return samples[count / 2];
}