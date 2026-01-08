#pragma once

#include <CSprite2d.h>
#include <CRGBA.h>

namespace ButtonIcons {

// Icon size (in font-relative units)
constexpr float ICON_SIZE = 20.0f;

// ============================================================================
// KEYBOARD KEY INDICES
// Sprite indices 8-55 in the extended array
// ============================================================================

enum KeyboardKey {
    // Movement keys (indices 8-15)
    KEY_W = 0,
    KEY_A,
    KEY_S,
    KEY_D,
    KEY_UP,
    KEY_DOWN,
    KEY_LEFT,
    KEY_RIGHT,
    
    // Letter keys (indices 16-26)
    KEY_E,
    KEY_Q,
    KEY_F,
    KEY_G,
    KEY_H,
    KEY_N,
    KEY_Y,
    KEY_X,
    KEY_Z,
    KEY_V,
    KEY_C,
    
    // Numpad (indices 27-37)
    KEY_NUM0,
    KEY_NUM1,
    KEY_NUM2,
    KEY_NUM3,
    KEY_NUM4,
    KEY_NUM5,
    KEY_NUM6,
    KEY_NUM7,
    KEY_NUM8,
    KEY_NUM9,
    KEY_NUMDOT,
    
    // Modifiers (indices 38-41)
    KEY_LCTRL,
    KEY_RCTRL,
    KEY_LSHIFT,
    KEY_LALT,
    
    // Special keys (indices 42-51)
    KEY_SPACE,
    KEY_TAB,
    KEY_CAPSLOCK,
    KEY_DEL,
    KEY_HOME,
    KEY_END,
    KEY_PGUP,
    KEY_PGDN,
    KEY_RETURN,
    KEY_ENT,
    
    KEYBOARD_COUNT
};

// ============================================================================
// MOUSE BUTTON INDICES
// Sprite indices 56-62 in the extended array
// ============================================================================

enum MouseButton : int {
    MOUSE_LMB = 0,
    MOUSE_RMB = 1,
    MOUSE_MMB = 2,
    MOUSE_WHEEL_UP = 3,
    MOUSE_WHEEL_DOWN = 4,
    MOUSE_BUTTON_4 = 5,
    MOUSE_BUTTON_5 = 6,
    MOUSE_COUNT = 7
};

// ============================================================================
// PUBLIC API
// ============================================================================

void InstallHooks();
bool IsEnabled();
void SetEnabled(bool enabled);
void ReloadTextures();

// ============================================================================
// DIRECT DRAWING API
// ============================================================================

void DrawIcon(MouseButton button, float x, float y, float size);
void DrawIconColored(MouseButton button, float x, float y, float size,
                     unsigned char r, unsigned char g, unsigned char b, unsigned char a);

} // namespace ButtonIcons
