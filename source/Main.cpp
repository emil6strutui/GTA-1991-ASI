#include <plugin.h>
#include "CHud1991.h"
#include "Markers.h"
#include "AnimPatches.h"
#include "ButtonIcons.h"

using namespace plugin;

// ============================================================================
// GLOBAL HUD LAYOUT INSTANCE
// ============================================================================

// ============================================================================
// PLUGIN ENTRY POINT
// ============================================================================

class RoundedHUD {
public:
    RoundedHUD() {
        // Install all hooks - layout defaults are defined in HudLayout.h
        CHud1991::InstallHooks();
        //Markers::InstallHooks();
        //AnimPatches::InstallHooks();
        ButtonIcons::InstallHooks();
    }
} roundedHUD;
