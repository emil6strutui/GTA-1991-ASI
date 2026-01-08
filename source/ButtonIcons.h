#pragma once

#include <CSprite2d.h>
#include <CRGBA.h>

namespace ButtonIcons {

// Icon size (in font-relative units)
constexpr float ICON_SIZE = 20.0f;

// ============================================================================
// MOUSE BUTTON INDICES (use these with ~b~X~ tokens)
// ============================================================================

enum MouseButton : int {
    MOUSE_LMB = 0,          
    MOUSE_RMB = 1,          // ~b~1~ Right mouse button
    MOUSE_MMB = 2,          // ~b~2~ Middle mouse button
    MOUSE_WHEEL_UP = 3,     // ~b~3~ Scroll wheel up
    MOUSE_WHEEL_DOWN = 4,   // ~b~4~ Scroll wheel down
    MOUSE_BUTTON_4 = 5,     // ~b~5~ Extra button 4
    MOUSE_BUTTON_5 = 6,     // ~b~6~ Extra button 5
    MOUSE_COUNT = 7
};

// ============================================================================
// PUBLIC API
// ============================================================================

// Install hooks - call from Main.cpp
void InstallHooks();

// Check if button icons system is enabled
bool IsEnabled();

// Enable/disable the button icons system
void SetEnabled(bool enabled);

// Hot-reload the TXD file
void ReloadTextures();

// ============================================================================
// DIRECT DRAWING API (for manual placement)
// ============================================================================

// Draw a mouse button icon at screen position (448-reference coordinates)
void DrawIcon(MouseButton button, float x, float y, float size);

// Draw with custom color
void DrawIconColored(MouseButton button, float x, float y, float size,
                     unsigned char r, unsigned char g, unsigned char b, unsigned char a);

} // namespace ButtonIcons
