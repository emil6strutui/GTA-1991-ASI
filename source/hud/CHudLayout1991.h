#pragma once

// ============================================================================
// HUD LAYOUT CONFIGURATION
// All positions use 640x448 reference coordinates
// ============================================================================

#define HUD_REF_WIDTH   640.0f
#define HUD_REF_HEIGHT  448.0f

#include <skeleton.h>
#include <CRGBA.h>

struct CHudLayout1991 {
    // ---- Top-right HUD positioning ----
    float weaponRightMargin = 74.0f;          // distance from right screen edge
    float statsRightMargin = weaponRightMargin + 10.0f;
    float hudStartY = 24.0f;             // Y position of first element
    float elementSpacing = 1.0f;        // vertical gap between element groups
    float textLineHeight = 0.85f;       // text height multiplier (compensates for font padding)

    // Weapon icon (also used by game patches - keep in sync)
    float weaponWidth = 58.0f;
    float weaponHeight = 58.0f;
    float ammoOffsetY = 0.6f;   // proportion of weaponHeight (0.0 = top, 1.0 = bottom)

    // Stat bars - dimensions
    float barWidth = 68.0f;
    float barHeight = 7.0f;
    float barBorderWidth = 2.0f;
    float barSpacing = 1.8f;            // gap between bars
    int barSegments = 11;               // semicircle smoothness (higher = smoother)
    int barCapSlices = 8;               // partial fill smoothness

    // Stat bars - colors
    CRGBA healthFG = CRGBA(0, 104, 131, 255);
    CRGBA healthBG = CRGBA(0, 52, 54, 180);
    CRGBA armorFG = CRGBA(225, 225, 225, 255);
    CRGBA armorBG = CRGBA(112, 112, 112, 180);
    CRGBA breathFG = CRGBA(172, 142, 104, 255);
    CRGBA breathBG = CRGBA(86, 71, 52, 180);
    CRGBA barBorderColor = CRGBA(0, 0, 0, 255);

    // Unified radar housing and wanted-level police siren
    // Width is Y-scaled in CHud1991.cpp so the viewport keeps this aspect ratio.
    float radarLeftMargin = 20.0f; // visual margin, calibrated against the weapon icon
    float radarViewportBottomMargin = 28.0f;
    float radarViewportWidth = 83.0f;
    float radarViewportHeight = 63.0f;
    float radarHousingPadding = 1.5f; // with the 1.25 outline: 2.25 total on left/right/bottom
    float radarSirenBezelHeight = 10.0f;
    float radarSirenMapGap = 5.0f; // visual clearance for blips centered on the map's top edge
    float radarHousingCornerRadius = 2.5f;
    float radarHousingBorder = 1.25f;
    float radarSirenInset = 3.0f;
    float radarNorthMarkerInset = 3.0f;

    float wantedSirenLightHeight = 4.0f;
    float wantedSirenLightSpacing = 1.8f;
    float wantedSirenLightPadding = 2.0f;
    float wantedSirenLightBorder = 0.45f;
    float wantedSirenGlowSize = 0.65f;
    unsigned int wantedSirenPulseMs = 180;

    CRGBA radarHousing = CRGBA(8, 10, 15, 245);
    CRGBA radarHousingEdge = CRGBA(0, 0, 0, 255);
    CRGBA wantedSirenRed = CRGBA(255, 32, 64, 255);
    CRGBA wantedSirenBlue = CRGBA(32, 112, 255, 255);
    CRGBA wantedSirenLightEdge = CRGBA(0, 0, 0, 230);

    // Visibility
    bool showClock = true;
    bool showMoney = true;
    bool showWeapon = true;
    bool showAmmo = true;
    bool showHealthBar = true;
    bool showArmorBar = true;
    bool showBreathBar = true;
    bool showWantedSirens = true;
};

extern CHudLayout1991 HudLayout;

// ============================================================================
// SCREEN SCALING HELPERS
// ============================================================================

namespace Screen {

inline float StretchX(float x) {
    return x * static_cast<float>(RsGlobal.maximumWidth) / HUD_REF_WIDTH;
}

inline float StretchY(float y) {
    return y * static_cast<float>(RsGlobal.maximumHeight) / HUD_REF_HEIGHT;
}

inline float FromRight(float x) {
    return static_cast<float>(RsGlobal.maximumWidth) - StretchY(x);
}

inline float GetBarX(float barWidth) {
    float barX = FromRight(HudLayout.statsRightMargin) - barWidth;
    return (barX < 0.0f) ? 0.0f : barX;
}

// Font scale multipliers (read from game memory, respects widescreen fixes)
inline float GetClockFontScaleY() {
    return *reinterpret_cast<float*>(0x858F14);  // flt_858F14
}

// Text height in reference coords: scaleY * 18 (CFont::GetHeight formula)
inline float GetTextHeight(float scaleY) {
    return scaleY * 17.0f;
}

} // namespace Screen
