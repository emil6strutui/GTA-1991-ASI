#pragma once

#include <skeleton.h>

// ============================================================================
// HUD LAYOUT CONFIGURATION
// All positions are in 640x448 reference coordinates
// ============================================================================

struct HudLayout {
    // Right margin from screen edge
    float rightMargin = 65.0f;
    
    // Clock position
    float clockY = 22.0f;
    
    // Money position  
    float moneyY = 40.0f;
    
    // Weapon icon position (relative to right edge)
    float weaponX = 0.0f;
    float weaponY = 22.0f;
    float weaponWidth = 58.0f;
    float weaponHeight = 58.0f;
    
    // Ammo position (below weapon icon)
    float ammoY = 55.0f;
    
    // Bar dimensions
    float barWidth = 64.0f;
    float barHeight = 8.0f;
    
    // Bar Y positions
    float healthBarY = 64.0f;
    float armorBarY = 76.0f;
    float breathBarY = 88.0f;
    
    // Wanted stars (above radar, left side)
    float starsX = 10.0f;           // X position from left (Y-scaled)
    float starsY = 320.0f;          // Y position from top
    float starSpacing = 20.0f;      // Spacing between stars
    float starScaleX = 0.53f;      // Font scale X
    float starScaleY = 0.95f;       // Font scale Y
    float starEmptyScale = 1.2f;    // Empty stars are larger by this multiplier
    float starEmptyYOffset = 2.0f;  // Y offset for empty stars
    
    // Visibility flags
    bool showClock = true;
    bool showMoney = true;
    bool showWeapon = true;
    bool showAmmo = true;
    bool showHealthBar = true;
    bool showArmorBar = true;
    bool showBreathBar = true;
    bool showStars = true;
};

// Global layout instance
extern HudLayout g_HudLayout;

// ============================================================================
// SCREEN HELPER FUNCTIONS
// ============================================================================

namespace Screen {

// Scale X coordinate for current resolution (reference: 640)
inline float StretchX(float x) {
    return x * static_cast<float>(RsGlobal.maximumWidth) / 640.0f;
}

// Scale Y coordinate for current resolution (reference: 448)
inline float StretchY(float y) {
    return y * static_cast<float>(RsGlobal.maximumHeight) / 448.0f;
}

// Get X position from right edge of screen (Y-scaled for consistent offset)
inline float FromRight(float x) {
    return static_cast<float>(RsGlobal.maximumWidth) - StretchY(x);
}

// Get X position for right-aligned bar
// Margin is X-scaled (aligns with clock/money), width is Y-scaled (passed in)
inline float GetBarX(float barWidth) {
    float rightEdge = FromRight(g_HudLayout.rightMargin);
    float barX = rightEdge - barWidth;
    // Clamp to prevent going off-screen at extreme aspect ratios
    if (barX < 0.0f) barX = 0.0f;
    return barX;
}

} // namespace Screen

