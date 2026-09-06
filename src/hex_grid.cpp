#include "hex_grid.h"

#include <math.h>

const HexTile HEX_TILES[9] = {
    { "R00",  38.5f,  100.0f, false, -1 },
    { "R01", 115.5f,  100.0f, false, -1 },
    { "R02",   0.0f,   33.3f, true,   0 },
    { "R03",  77.0f,   33.3f, true,   1 },
    { "R04", 154.0f,   33.3f, false, -1 },
    { "R05",  38.5f,  -33.3f, true,   3 },
    { "R06", 115.5f,  -33.3f, true,   5 },
    { "R07",   0.0f, -100.0f, true,   4 },
    { "R08",  77.0f, -100.0f, true,   2 },
};

namespace {

constexpr float SQRT_3 = 1.7320508f;

// Circumradius (center-to-vertex): for a regular hexagon,
// width-across-flats = sqrt(3) * circumradius.
constexpr float HEX_CIRCUMRADIUS_MM = HEX_FLAT_WIDTH_MM / SQRT_3;

// Ratio of apothem (center-to-flat-edge) to circumradius, i.e. cos(30deg)
// -- used for hit-testing (see hexGridHitTest).
constexpr float HEX_APOTHEM_RATIO = 0.8660254f;

constexpr float LAYOUT_MARGIN_FACTOR = 0.92f; // breathing room at the content edges

struct TileLayout {
    int cx = 0, cy = 0; // pixel center
    int r = 0;          // pixel circumradius, as drawn
};

TileLayout lastLayout[HEX_TILE_COUNT];
bool haveLayout = false;
HexTileTapHandler tapHandler = nullptr;

void hexVertex(int cx, int cy, int r, int i, int& vx, int& vy) {
    // 30-degree start -> pointy-top hexagons (vertices up/down, flat edges
    // left/right), matching the Blender CAD script's HEX_ANGLES and the
    // tile-center spacing derived from it. A 0-degree start (flat-top)
    // mismatches that spacing and produces overlapping/gapping tiles.
    float angleRad = (30.0f + 60.0f * i) * (PI / 180.0f);
    vx = cx + (int)roundf(r * cosf(angleRad));
    vy = cy + (int)roundf(r * sinf(angleRad));
}

void drawFilledHex(TFT_eSPI& tft, int cx, int cy, int r, uint16_t fillColor, uint16_t outlineColor) {
    int vx[6], vy[6];
    for (int i = 0; i < 6; i++) hexVertex(cx, cy, r, i, vx[i], vy[i]);

    for (int i = 0; i < 6; i++) {
        int j = (i + 1) % 6;
        tft.fillTriangle(cx, cy, vx[i], vy[i], vx[j], vy[j], fillColor);
    }
    for (int i = 0; i < 6; i++) {
        int j = (i + 1) % 6;
        tft.drawLine(vx[i], vy[i], vx[j], vy[j], outlineColor);
    }
}

void drawDashedLine(TFT_eSPI& tft, float x0, float y0, float x1, float y1, uint16_t color) {
    constexpr float DASH_LEN = 4.0f;
    constexpr float GAP_LEN = 3.0f;

    float dx = x1 - x0, dy = y1 - y0;
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 1.0f) return;

    float ux = dx / len, uy = dy / len;
    for (float pos = 0.0f; pos < len; pos += DASH_LEN + GAP_LEN) {
        float segEnd = (pos + DASH_LEN < len) ? pos + DASH_LEN : len;
        tft.drawLine((int)roundf(x0 + ux * pos), (int)roundf(y0 + uy * pos),
                      (int)roundf(x0 + ux * segEnd), (int)roundf(y0 + uy * segEnd), color);
    }
}

void drawDashedHex(TFT_eSPI& tft, int cx, int cy, int r, uint16_t color) {
    int vx[6], vy[6];
    for (int i = 0; i < 6; i++) hexVertex(cx, cy, r, i, vx[i], vy[i]);

    for (int i = 0; i < 6; i++) {
        int j = (i + 1) % 6;
        drawDashedLine(tft, vx[i], vy[i], vx[j], vy[j], color);
    }
}

} // namespace

void drawHexGrid(TFT_eSPI& tft, int contentX, int contentY, int contentW, int contentH,
                  int selectedIndex, HexTileTapHandler onTap) {
    tapHandler = onTap;

    // Bounding box of tile centers, in screen-space mm (CAD's +y-up
    // flipped to +y-down).
    float minX = 1e9f, maxX = -1e9f, minY = 1e9f, maxY = -1e9f;
    for (int i = 0; i < HEX_TILE_COUNT; i++) {
        float x = HEX_TILES[i].xMm;
        float y = -HEX_TILES[i].yMm;
        if (x < minX) minX = x;
        if (x > maxX) maxX = x;
        if (y < minY) minY = y;
        if (y > maxY) maxY = y;
    }

    // Pad by each hex's own extent so edge tiles aren't clipped. Pointy-top
    // orientation (see hexVertex) puts flat edges left/right and vertices
    // up/down, so the horizontal reach from center is the apothem
    // (half the flat width) and the vertical reach is the circumradius --
    // the opposite of what a flat-top hex would need here.
    minX -= HEX_FLAT_WIDTH_MM / 2.0f;
    maxX += HEX_FLAT_WIDTH_MM / 2.0f;
    minY -= HEX_CIRCUMRADIUS_MM;
    maxY += HEX_CIRCUMRADIUS_MM;

    float layoutWidthMm = maxX - minX;
    float layoutHeightMm = maxY - minY;

    float scaleX = (float)contentW / layoutWidthMm;
    float scaleY = (float)contentH / layoutHeightMm;
    float scale = (scaleX < scaleY ? scaleX : scaleY) * LAYOUT_MARGIN_FACTOR;

    float layoutCenterXmm = (minX + maxX) / 2.0f;
    float layoutCenterYmm = (minY + maxY) / 2.0f;
    int contentCenterX = contentX + contentW / 2;
    int contentCenterY = contentY + contentH / 2;

    int hexRadiusPx = (int)roundf(HEX_CIRCUMRADIUS_MM * scale);

    tft.setTextDatum(MC_DATUM);
    tft.setTextSize(2);

    for (int i = 0; i < HEX_TILE_COUNT; i++) {
        const HexTile& tile = HEX_TILES[i];
        float xMm = tile.xMm;
        float yMm = -tile.yMm;

        int cx = contentCenterX + (int)roundf((xMm - layoutCenterXmm) * scale);
        int cy = contentCenterY + (int)roundf((yMm - layoutCenterYmm) * scale);

        lastLayout[i] = { cx, cy, hexRadiusPx };

        if (tile.populated) {
            bool selected = (i == selectedIndex);
            uint16_t fillColor = selected ? TFT_ORANGE : TFT_DARKCYAN;
            uint16_t outlineColor = selected ? TFT_YELLOW : TFT_WHITE;
            drawFilledHex(tft, cx, cy, hexRadiusPx, fillColor, outlineColor);

            tft.setTextColor(TFT_WHITE, fillColor);
            String label = "Pad " + String(tile.sensorIndex + 1);
            tft.drawString(label, cx, cy);
        } else {
            drawDashedHex(tft, cx, cy, hexRadiusPx, TFT_DARKGREY);
        }
    }

    tft.setTextDatum(TL_DATUM);
    haveLayout = true;
}

int hexGridHitTest(int screenX, int screenY) {
    if (!haveLayout) return -1;

    for (int i = 0; i < HEX_TILE_COUNT; i++) {
        // Hit-test against the inscribed circle (apothem), not the
        // circumradius used for drawing -- adjacent tile centers are
        // spaced exactly one flat-width apart, so apothem-radius hit
        // circles are tangent rather than overlapping. Undershoots the
        // hexagon's pointed top/bottom corners slightly (pointy-top
        // orientation, see hexVertex), but avoids ambiguous double-hits
        // along shared edges. The apothem/circumradius ratio itself
        // (cos(30deg)) is rotation-invariant, so this needed no change
        // when hexVertex's angle offset changed.
        int hitR = (int)(lastLayout[i].r * HEX_APOTHEM_RATIO);
        int dx = screenX - lastLayout[i].cx;
        int dy = screenY - lastLayout[i].cy;
        if (dx * dx + dy * dy <= hitR * hitR) {
            return i;
        }
    }
    return -1;
}
