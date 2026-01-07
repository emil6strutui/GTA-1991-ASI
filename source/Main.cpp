#include <plugin.h>
#include "HudLayout.h"
#include "HudElements.h"
#include "Markers.h"
#include "AnimPatches.h"

using namespace plugin;

// ============================================================================
// GLOBAL HUD LAYOUT INSTANCE
// ============================================================================

HudLayout g_HudLayout;

// ============================================================================
// PLUGIN ENTRY POINT
// ============================================================================

class RoundedHUD {
public:
    RoundedHUD() {
        // Install all hooks - layout defaults are defined in HudLayout.h
        HudElements::InstallHooks();
        Markers::InstallHooks();
        AnimPatches::InstallHooks();
    }
} roundedHUD;

// ============================================================================
// EXPORTED API FUNCTIONS
// Can be called from CLEO scripts via DynamicLibrary.getProcedure
// ============================================================================

extern "C" {

__declspec(dllexport) void ConfigureHudLayout(
    float rightMargin, float clockY, float moneyY,
    float weaponX, float weaponY, float ammoY,
    float healthY, float armorY, float breathY
) {
    g_HudLayout.rightMargin = rightMargin;
    g_HudLayout.clockY = clockY;
    g_HudLayout.moneyY = moneyY;
    g_HudLayout.weaponX = weaponX;
    g_HudLayout.weaponY = weaponY;
    g_HudLayout.ammoY = ammoY;
    g_HudLayout.healthBarY = healthY;
    g_HudLayout.armorBarY = armorY;
    g_HudLayout.breathBarY = breathY;
}

__declspec(dllexport) void SetClockMoneyPos(float clockY, float moneyY) {
    g_HudLayout.clockY = clockY;
    g_HudLayout.moneyY = moneyY;
}

__declspec(dllexport) void SetWeaponPos(float x, float y, float width, float height) {
    g_HudLayout.weaponX = x;
    g_HudLayout.weaponY = y;
    g_HudLayout.weaponWidth = width;
    g_HudLayout.weaponHeight = height;
}

__declspec(dllexport) void SetBarYPositions(float healthY, float armorY, float breathY) {
    g_HudLayout.healthBarY = healthY;
    g_HudLayout.armorBarY = armorY;
    g_HudLayout.breathBarY = breathY;
}

__declspec(dllexport) void SetBarDimensions(float width, float height) {
    g_HudLayout.barWidth = width;
    g_HudLayout.barHeight = height;
}

__declspec(dllexport) void SetRightMargin(float margin) {
    g_HudLayout.rightMargin = margin;
}

__declspec(dllexport) void SetHudElementsVisible(
    bool clock, bool money, bool weapon, bool ammo,
    bool health, bool armor, bool breath
) {
    g_HudLayout.showClock = clock;
    g_HudLayout.showMoney = money;
    g_HudLayout.showWeapon = weapon;
    g_HudLayout.showAmmo = ammo;
    g_HudLayout.showHealthBar = health;
    g_HudLayout.showArmorBar = armor;
    g_HudLayout.showBreathBar = breath;
}

} // extern "C"
