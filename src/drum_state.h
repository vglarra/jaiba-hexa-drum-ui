#pragma once

#include <Arduino.h>

// Number of physical pads currently wired (indices 0-5, matching the drum
// sketch's sensors[] array). Update if more pads get built.
constexpr int NUM_PADS = 6;

struct PadState {
    int note = -1;
    int threshold = -1;
    int ceiling = -1;
    float curveExp = -1.0f;
    unsigned long lastUpdated = 0; // millis() of last PADVAL received; 0 = never
};

enum class PlayMode { UNKNOWN, BOTH, PIEZO_ONLY, VELOSTAT_ONLY };

enum class CalSensor { NONE, PIEZO, VELOSTAT };
enum class CalPhase { NONE, RESTING, WAITING_TOUCH, CAPTURING, PAD_DONE, COMPLETE, ABORTED };

struct CalState {
    CalSensor sensor = CalSensor::NONE;
    CalPhase phase = CalPhase::NONE;
    int padIndex = -1; // only meaningful when phase is CAPTURING or PAD_DONE
    unsigned long lastUpdated = 0;
};

struct DrumState {
    PadState pads[NUM_PADS];

    float crosstalkRatio = -1.0f;
    int crosstalkWindowMs = -1;
    unsigned long xtalkLastUpdated = 0;

    PlayMode playMode = PlayMode::UNKNOWN;
    unsigned long playModeLastUpdated = 0;

    CalState calState;
};

// Single global instance -- there is exactly one drum Teensy link. Screens
// read from this directly; only uart_protocol.cpp's message handlers write
// to it.
extern DrumState drumState;
