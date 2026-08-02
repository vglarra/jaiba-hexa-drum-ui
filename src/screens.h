#pragma once

#include <TFT_eSPI.h>

// Minimal screen manager: each screen is a case in switchScreen() (draws
// once, on entry) and updateScreen() (called every loop() iteration, for
// anything a screen needs to do on its own -- timers, animations, reading
// drumState). Screens don't own state beyond what a couple of statics in
// screens.cpp can hold; this is intentionally not a framework, just enough
// structure to keep adding screens from turning into a rewrite each time.
enum class ScreenId {
    SPLASH,
    LANDING_PLACEHOLDER,
};

// Call once from setup(), after tft.init(). Takes ownership of drawing to
// `display` and shows the initial screen (SPLASH).
void screensInit(TFT_eSPI& display);

// Switches to `id` immediately: draws its initial content and resets its
// per-screen timer/state.
void switchScreen(ScreenId id);

// Call every loop() iteration. Lets the current screen do per-frame work
// (e.g. the splash screen's auto-advance timer).
void updateScreen();
