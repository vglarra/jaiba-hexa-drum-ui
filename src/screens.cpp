#include "screens.h"

namespace {

// Splash has nothing real to wait on yet (no boot/init work gates it) --
// just a fixed minimum display time so it doesn't flash by unreadably
// fast, per UI_PLAN.md.
constexpr unsigned long SPLASH_MIN_DURATION_MS = 1800;

TFT_eSPI* tft = nullptr;
ScreenId currentScreen = ScreenId::SPLASH;
unsigned long screenEnteredAt = 0;

void drawCenteredLines(const char* line1, const char* line2, int textSize) {
    tft->fillScreen(TFT_BLACK);
    tft->setTextColor(TFT_WHITE);
    tft->setTextSize(textSize);
    tft->setTextDatum(MC_DATUM); // middle-center anchor

    int lineHeight = 8 * textSize + 6; // GLCD font is 8px tall before scaling
    int midX = tft->width() / 2;
    int midY = tft->height() / 2;

    if (line2 == nullptr) {
        tft->drawString(line1, midX, midY);
    } else {
        tft->drawString(line1, midX, midY - lineHeight / 2);
        tft->drawString(line2, midX, midY + lineHeight / 2);
    }

    tft->setTextDatum(TL_DATUM); // restore default anchor
}

void drawSplash() {
    drawCenteredLines("Initializing", "Jaiba Hexa Drum UI", 2);
}

void drawLandingPlaceholder() {
    drawCenteredLines("Landing page", "coming soon", 2);
}

void updateSplash() {
    if (millis() - screenEnteredAt >= SPLASH_MIN_DURATION_MS) {
        switchScreen(ScreenId::LANDING_PLACEHOLDER);
    }
}

} // namespace

void screensInit(TFT_eSPI& display) {
    tft = &display;
    switchScreen(ScreenId::SPLASH);
}

void switchScreen(ScreenId id) {
    currentScreen = id;
    screenEnteredAt = millis();

    switch (id) {
        case ScreenId::SPLASH: drawSplash(); break;
        case ScreenId::LANDING_PLACEHOLDER: drawLandingPlaceholder(); break;
    }
}

void updateScreen() {
    switch (currentScreen) {
        case ScreenId::SPLASH: updateSplash(); break;
        case ScreenId::LANDING_PLACEHOLDER: break; // no interaction/timer yet
    }
}
