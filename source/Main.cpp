#include <plugin.h>
#include "CHud1991.h"
#include "Markers.h"
#include "CStoriesSprinting.h"
#include "ButtonIcons.h"
#include "CKeybindings.h"

using namespace plugin;

// ============================================================================
// PLUGIN ENTRY POINT
// ============================================================================

class MainPlugin {
public:
    MainPlugin() {
        // Patch user directory string
        // Original: "\GTA San Andreas User Files" at 0x008747A8
        const char* newString = "\\GTA 1991 User Files";
        patch::SetRaw(0x008747A8, (void*)newString, strlen(newString) + 1);

        // Install all hooks - layout defaults are defined in HudLayout.h
        CHud1991::InstallHooks();
        Markers::InstallHooks();
        CStoriesSprinting::InstallHooks();
        ButtonIcons::InstallHooks();
        CKeybindings::InstallPatches();
    }
} mainPlugin;
