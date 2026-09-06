#pragma once

#include <TFT_eSPI.h>

// One of the drum shell's 9 right-hemisphere tile positions, in mm, using
// the drum CAD's coordinate convention (+y is up -- flipped to screen
// space, +y down, when rendered). Fixed design-time layout pulled from the
// drum project's Blender CAD, not computed at runtime.
struct HexTile {
    const char* id;   // e.g. "R02" -- reference/debugging only, not shown
    float xMm;
    float yMm;
    bool populated;
    int sensorIndex;  // 0-based UART sensor index; -1 when not populated
};

extern const HexTile HEX_TILES[9];
constexpr int HEX_TILE_COUNT = 9;

// Width across flats (mm) of each hexagonal tile, from the drum CAD.
constexpr float HEX_FLAT_WIDTH_MM = 77.0f;

// Invoked with a HEX_TILES index (0..HEX_TILE_COUNT-1) when a tile is
// tapped. Nothing calls this yet -- accepted and stored so the landing
// page and Pad Assignment can both reuse this component for tap-to-select
// once they own touch input.
using HexTileTapHandler = void (*)(int tileIndex);

// Renders all 9 tiles, uniformly scaled and centered to fit inside the
// given content rectangle (screen px). Callers should size that rectangle
// to leave room for a footer below it -- this component only fills what
// it's given. `selectedIndex` (a HEX_TILES index, or -1 for none)
// highlights one populated tile; `onTap` is stored for hexGridHitTest()'s
// future caller, not invoked here.
void drawHexGrid(TFT_eSPI& tft, int contentX, int contentY, int contentW, int contentH,
                  int selectedIndex = -1, HexTileTapHandler onTap = nullptr);

// Returns the HEX_TILES index whose hexagon contains (screenX, screenY),
// or -1 if none -- uses the layout computed by the most recent
// drawHexGrid() call. Not called by anything yet; here so a future
// screen's touch handling doesn't need to reimplement hex hit-testing.
int hexGridHitTest(int screenX, int screenY);
