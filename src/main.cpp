#include <Arduino.h>
#include <TFT_eSPI.h>

// ============================================================
// UART LINK TEST — Teensy 4.1, ST7796 display
//
// Temporary test screen: receives pad-hit messages from the drum Teensy
// over Serial1 (RX1=pin 0, TX1=pin 1) and displays the pad label large
// and centered the moment a "PAD,<label>" line arrives.
//
// Touch-test behavior (previous main.cpp) is not running while this test
// is active — see git history to restore it once the UART link is verified.
// ============================================================

TFT_eSPI tft = TFT_eSPI();

#define UART_LINE_MAX 63

void showPadLabel(const String &label);

void setup() {
    Serial.begin(9600);
    while (!Serial && millis() < 2000); // don't hang forever if no monitor is attached

    Serial1.begin(115200); // UART link from drum Teensy

    tft.init();
    tft.setRotation(0);
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(2);
    tft.setCursor(10, 10);
    tft.println("Waiting for pad hit...");
}

void loop() {
    static String line;

    while (Serial1.available()) {
        char c = Serial1.read();

        if (c == '\n') {
            line.trim(); // drop trailing \r and stray whitespace

            Serial.print("Serial1 << ");
            Serial.println(line);

            if (line.startsWith("PAD,")) {
                String label = line.substring(4);
                label.trim();

                Serial.print("Pad hit: ");
                Serial.println(label);

                showPadLabel(label);
            }

            line = "";
        } else {
            line += c;
            if (line.length() > UART_LINE_MAX) line = ""; // discard garbage/noise line
        }
    }

    // Test-command passthrough: forward lines typed into the USB Serial
    // Monitor to the drum Teensy over Serial1, for protocol testing.
    static String cmdLine;

    while (Serial.available()) {
        char c = Serial.read();

        if (c == '\n') {
            cmdLine.trim(); // drop trailing \r and stray whitespace

            if (cmdLine.length() > 0) {
                Serial1.print(cmdLine);
                Serial1.print('\n');

                Serial.print("Serial1 >> ");
                Serial.println(cmdLine);
            }

            cmdLine = "";
        } else {
            cmdLine += c;
            if (cmdLine.length() > UART_LINE_MAX) cmdLine = ""; // discard garbage/noise line
        }
    }
}

void showPadLabel(const String &label) {
    tft.fillScreen(TFT_BLACK);

    tft.setTextSize(4);
    tft.setTextColor(TFT_WHITE);
    tft.setTextDatum(MC_DATUM); // middle-center anchor
    tft.drawString(label, tft.width() / 2, tft.height() / 2);
    tft.setTextDatum(TL_DATUM); // restore default anchor
}
