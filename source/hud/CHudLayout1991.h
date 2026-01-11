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
    // Right margin
    float rightMargin = 65.0f;

    // Clock & Money
    float clockY = 22.0f;
    float moneyY = 40.0f;

    // Weapon icon
    float weaponX = 0.0f;
    float weaponY = 22.0f;
    float weaponWidth = 58.0f;
    float weaponHeight = 58.0f;
    float ammoY = 55.0f;

    // Stat bars - dimensions
    float barWidth = 68.0f;
    float barHeight = 9.0f;
    float barBorderWidth = 1.5f;
    float barStartY = 64.0f;    // first bar Y position
    float barSpacing = 1.8f;    // gap between bars
    int barSegments = 11;       // semicircle smoothness (higher = smoother)
    int barCapSlices = 8;       // partial fill smoothness

    // Stat bars - colors
    CRGBA healthFG = CRGBA(0, 104, 131, 255);
    CRGBA healthBG = CRGBA(0, 52, 54, 180);
    CRGBA armorFG = CRGBA(225, 225, 225, 255);
    CRGBA armorBG = CRGBA(112, 112, 112, 180);
    CRGBA breathFG = CRGBA(172, 142, 104, 255);
    CRGBA breathBG = CRGBA(86, 71, 52, 180);
    CRGBA barBorderColor = CRGBA(0, 0, 0, 255);

    // Wanted stars
    float starsX = 10.0f;
    float starsY = 320.0f;
    float starSpacing = 20.0f;
    float starScaleX = 0.53f;
    float starScaleY = 0.95f;
    float starEmptyScale = 1.2f;
    float starEmptyYOffset = 2.0f;

    // Visibility
    bool showClock = true;
    bool showMoney = true;
    bool showWeapon = true;
    bool showAmmo = true;
    bool showHealthBar = true;
    bool showArmorBar = true;
    bool showBreathBar = true;
    bool showStars = true;
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
    float barX = FromRight(HudLayout.rightMargin) - barWidth;
    return (barX < 0.0f) ? 0.0f : barX;
}

} // namespace Screen
