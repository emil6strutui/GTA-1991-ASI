#include "Markers.h"

#include <plugin.h>
#include <CVector.h>

using namespace plugin;

namespace Markers {

// ============================================================================
// CONFIGURATION
// ============================================================================

CRGBA EnexColor(0, 104, 131, 255);  // Dirty Crystal Teal

// ============================================================================
// ENEX MARKER HOOK
// ============================================================================

using PlaceMarkerCone_t = void(__cdecl*)(
    uint32_t id,
    CVector& point,
    float size,
    uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha,
    uint16_t pulsePeriod,
    float pulseFraction,
    int16_t rotateRate,
    bool bEnableCollision
);

static PlaceMarkerCone_t PlaceMarkerCone_Original = nullptr;

void __cdecl PlaceMarkerCone_Hook(
    uint32_t id,
    CVector& point,
    float size,
    uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha,
    uint16_t pulsePeriod,
    float pulseFraction,
    int16_t rotateRate,
    bool bEnableCollision
) {
    // This hook only intercepts ENEX markers (hooked at 0x440F4E in CEntryExitManager::Update)
    // so we can apply our customizations unconditionally
    PlaceMarkerCone_Original(id, point, size * 0.75f, 
                             EnexColor.r, EnexColor.g, EnexColor.b, EnexColor.a,
                             pulsePeriod, pulseFraction, rotateRate, bEnableCollision);
}

// ============================================================================
// INSTALL HOOKS
// ============================================================================

void InstallHooks() {
    // Hook the PlaceMarkerCone call from CEntryExitManager::Update
    // Original function is C3dMarkers::PlaceMarkerCone at 0x726D40
    PlaceMarkerCone_Original = reinterpret_cast<PlaceMarkerCone_t>(0x726D40);
    patch::RedirectCall(0x440F4E, PlaceMarkerCone_Hook);
}

} // namespace Markers
