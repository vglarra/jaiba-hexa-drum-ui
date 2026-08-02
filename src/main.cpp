#include <Arduino.h>
#include <TFT_eSPI.h>

#include "drum_state.h"
#include "screens.h"
#include "uart_protocol.h"

// ============================================================
// Real UART message parsing (drum_state.h, uart_protocol.h/.cpp) and the
// first real UI screen (screens.h/.cpp, starting with the splash screen
// per UI_PLAN.md) now replace the old throwaway PAD,<label> test protocol
// and its static status text. The Serial passthrough debug tooling below
// stays in place for manual protocol testing while more screens get built.
// ============================================================

TFT_eSPI tft = TFT_eSPI();

#define UART_LINE_MAX 63

// ---- Temporary debug dump, for validating uart_protocol/drum_state by
// hand before any screen exists to display this visually. Type "DUMP"
// into the Serial Monitor to print current drumState. Remove once a real
// screen reads this state on its own. ----

const char* playModeStr(PlayMode m) {
    switch (m) {
        case PlayMode::BOTH: return "BOTH";
        case PlayMode::PIEZO_ONLY: return "PIEZO_ONLY";
        case PlayMode::VELOSTAT_ONLY: return "VELOSTAT_ONLY";
        default: return "UNKNOWN";
    }
}

const char* calSensorStr(CalSensor s) {
    switch (s) {
        case CalSensor::PIEZO: return "PIEZO";
        case CalSensor::VELOSTAT: return "VELOSTAT";
        default: return "NONE";
    }
}

const char* calPhaseStr(CalPhase p) {
    switch (p) {
        case CalPhase::RESTING: return "RESTING";
        case CalPhase::WAITING_TOUCH: return "WAITING_TOUCH";
        case CalPhase::CAPTURING: return "CAPTURING";
        case CalPhase::PAD_DONE: return "PAD_DONE";
        case CalPhase::COMPLETE: return "COMPLETE";
        case CalPhase::ABORTED: return "ABORTED";
        default: return "NONE";
    }
}

const char* commandResultStr(CommandResult r) {
    switch (r) {
        case CommandResult::PENDING: return "PENDING";
        case CommandResult::SUCCESS: return "SUCCESS";
        case CommandResult::FAILED: return "FAILED";
        case CommandResult::TIMED_OUT: return "TIMED_OUT";
        default: return "NONE";
    }
}

void printDrumStateDebug() {
    Serial.println("---- drumState ----");
    for (int i = 0; i < NUM_PADS; i++) {
        PadState& pad = drumState.pads[i];
        Serial.print("pad "); Serial.print(i);
        Serial.print(": note="); Serial.print(pad.note);
        Serial.print(" threshold="); Serial.print(pad.threshold);
        Serial.print(" ceiling="); Serial.print(pad.ceiling);
        Serial.print(" curveExp="); Serial.print(pad.curveExp);
        Serial.print(" lastUpdated="); Serial.println(pad.lastUpdated);
    }

    Serial.print("crosstalk: ratio="); Serial.print(drumState.crosstalkRatio);
    Serial.print(" windowMs="); Serial.print(drumState.crosstalkWindowMs);
    Serial.print(" lastUpdated="); Serial.println(drumState.xtalkLastUpdated);

    Serial.print("playMode: "); Serial.print(playModeStr(drumState.playMode));
    Serial.print(" lastUpdated="); Serial.println(drumState.playModeLastUpdated);

    Serial.print("calState: sensor="); Serial.print(calSensorStr(drumState.calState.sensor));
    Serial.print(" phase="); Serial.print(calPhaseStr(drumState.calState.phase));
    Serial.print(" padIndex="); Serial.print(drumState.calState.padIndex);
    Serial.print(" lastUpdated="); Serial.println(drumState.calState.lastUpdated);

    Serial.print("lastCommandResult: "); Serial.println(commandResultStr(getLastCommandResult()));
    Serial.print("lastFailureReason: "); Serial.println(getLastFailureReason());
    Serial.println("-------------------");
}

void setup() {
    Serial.begin(9600);
    while (!Serial && millis() < 2000); // don't hang forever if no monitor is attached

    uartProtocolInit();

    tft.init();
    tft.setRotation(0);
    screensInit(tft);
}

void loop() {
    uartProtocolUpdate();
    updateScreen();

    // Test-command passthrough: forward lines typed into the USB Serial
    // Monitor to the drum Teensy over Serial1, for manual protocol testing
    // (e.g. CAL_START,PIEZO,SINGLE,2) ahead of real screens sending these.
    static String cmdLine;

    while (Serial.available()) {
        char c = Serial.read();

        if (c == '\n') {
            cmdLine.trim(); // drop trailing \r and stray whitespace

            if (cmdLine == "DUMP") {
                printDrumStateDebug();
            } else if (cmdLine.length() > 0) {
                sendCommand(cmdLine);

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
