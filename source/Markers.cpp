#include "Markers.h"

#include <plugin.h>
#include <Events.h>
#include <CVector.h>
#include <CMatrix.h>
#include <CEntryExit.h>
#include <CEntryExitManager.h>
#include <C3dMarker.h>
#include <C3dMarkers.h>
#include <CEntity.h>
#include <CFileLoader.h>
#include <unordered_map>
#include <string>

using namespace plugin;

namespace Markers {

// ============================================================================
// CONFIGURATION
// ============================================================================

CRGBA EnexColor(0, 104, 131, 255);  // Default: Dirty Crystal Teal

// ============================================================================
// EXTENDED ENEX DATA (per-ENEX customization)
// ============================================================================

struct EnexCustomData {
    CRGBA color;
    std::string modelName;
    bool hasCustomData;
    RpClump* customClump;  // Cached loaded model
    
    EnexCustomData() : color(0, 104, 131, 255), modelName("diamond_3"), 
                       hasCustomData(false), customClump(nullptr) {}
};

// Map from CEntryExit pointer to custom data
static std::unordered_map<CEntryExit*, EnexCustomData> g_EnexCustomData;

// Original LoadEntryExit function - returns CEntryExit* (SDK declares void but it actually returns)
using LoadEntryExit_t = CEntryExit*(__cdecl*)(const char* line);
static LoadEntryExit_t LoadEntryExit_Original = nullptr;

// Hook for CFileLoader::LoadEntryExit
CEntryExit* __cdecl LoadEntryExit_Hook(const char* line) {
    // Call original to create the ENEX - it returns the pointer directly!
    CEntryExit* newEnex = LoadEntryExit_Original(line);
    
    if (!newEnex) return newEnex;
    
    // Try to parse extended format after the standard 18 values
    // Standard format: 18 values ending with timeOff
    // Extended format: ... timeOff R G B A model_name
    
    int r = 0, g = 104, b = 131, a = 255;
    char modelName[64] = "diamond_3";
    
    float f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11;
    int d1, d2, d3, d4, d5, d6;
    char name[64];
    
    // Try parsing extended format (standard 18 + our 5 = 23 values)
    int parsed = sscanf(line,
        "%f %f %f %f %f %f %f %f %f %f %f %d %d %63s %d %d %d %d %d %d %d %d %63s",
        &f1, &f2, &f3, &f4, &f5, &f6, &f7, &f8, &f9, &f10, &f11,  // 11 floats
        &d1, &d2,  // area, flags
        name,      // interior name
        &d3, &d4, &d5, &d6,  // skyColor, numOfPeds, timeOn, timeOff
        &r, &g, &b, &a,       // OUR CUSTOM: R G B A
        modelName             // OUR CUSTOM: model_name
    );
    
    bool hasExtendedData = (parsed >= 22);  // At least R G B A parsed
    
    if (hasExtendedData) {
        EnexCustomData data;
        data.color = CRGBA(r, g, b, a);
        data.modelName = modelName;
        data.hasCustomData = true;
        data.customClump = nullptr;
        g_EnexCustomData[newEnex] = data;
    }
    
    return newEnex;
}

// ============================================================================
// MARKER MODEL MANAGEMENT
// ============================================================================

static RpClump* g_DefaultCustomClump = nullptr;
static RpClump* g_OriginalConeClump = nullptr;
static RpClump* g_OriginalConeNoCollClump = nullptr;
static bool g_ModelInitialized = false;

// Lazy initialization - only load on first use when marker system is ready
void EnsureModelLoaded() {
    if (g_ModelInitialized) return;
    
    RpClump** clumpArray = C3dMarkers::m_pRpClumpArray;
    if (!clumpArray || !clumpArray[MARKER3D_CONE]) return;
    
    // Store original clumps
    g_OriginalConeClump = clumpArray[MARKER3D_CONE];
    g_OriginalConeNoCollClump = clumpArray[MARKER3D_CONE_NO_COLLISION];
    
    // Load default custom model (diamond_4), fallback to diamond_3 on failure
    try {
        g_DefaultCustomClump = C3dMarkers::LoadMarker("diamond_4");
    } catch (...) {
        g_DefaultCustomClump = nullptr;
    }
    
    if (!g_DefaultCustomClump) {
        try {
            g_DefaultCustomClump = C3dMarkers::LoadMarker("diamond_3");
        } catch (...) {
            g_DefaultCustomClump = nullptr;
        }
    }
    
    g_ModelInitialized = true;
}

// Get or load custom model for an ENEX
static RpClump* GetCustomModelForEnex(CEntryExit* enex) {
    auto it = g_EnexCustomData.find(enex);
    if (it == g_EnexCustomData.end() || !it->second.hasCustomData) {
        return nullptr;
    }
    
    EnexCustomData& data = it->second;
    
    // Load custom model if not already loaded, fallback to diamond_3 on failure
    if (!data.customClump && !data.modelName.empty()) {
        try {
            data.customClump = C3dMarkers::LoadMarker(data.modelName.c_str());
        } catch (...) {
            data.customClump = nullptr;
        }
        
        if (!data.customClump) {
            try {
                data.customClump = C3dMarkers::LoadMarker("diamond_3");
            } catch (...) {
                data.customClump = nullptr;
            }
        }
    }
    
    return data.customClump;
}

// ============================================================================
// ENEX MARKER HOOK
// ============================================================================

using PlaceMarkerCone_t = void(__cdecl*)(
    uint32_t id, CVector& point, float size,
    uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha,
    uint16_t pulsePeriod, float pulseFraction, int16_t rotateRate,
    bool bEnableCollision
);

static PlaceMarkerCone_t PlaceMarkerCone_Original = nullptr;

void __cdecl PlaceMarkerCone_Hook(
    uint32_t id, CVector& point, float size,
    uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha,
    uint16_t pulsePeriod, float pulseFraction, int16_t rotateRate,
    bool bEnableCollision
) {
    // The 'id' parameter is actually the CEntryExit pointer cast to uint32
    CEntryExit* enex = reinterpret_cast<CEntryExit*>(id);
    
    // Get per-ENEX custom data (or use defaults)
    CRGBA markerColor = EnexColor;
    RpClump* customClump = nullptr;
    
    auto it = g_EnexCustomData.find(enex);
    if (it != g_EnexCustomData.end() && it->second.hasCustomData) {
        markerColor = it->second.color;
        customClump = GetCustomModelForEnex(enex);
    } else {
        EnsureModelLoaded();
        customClump = g_DefaultCustomClump;
    }
    
    // Swap to custom model if available
    RpClump** clumpArray = C3dMarkers::m_pRpClumpArray;
    RpClump* savedCone = clumpArray ? clumpArray[MARKER3D_CONE] : nullptr;
    RpClump* savedConeNoColl = clumpArray ? clumpArray[MARKER3D_CONE_NO_COLLISION] : nullptr;
    
    if (customClump && clumpArray) {
        clumpArray[MARKER3D_CONE] = customClump;
        clumpArray[MARKER3D_CONE_NO_COLLISION] = customClump;
    }
    
    // Place the marker with custom color and scaled size
    PlaceMarkerCone_Original(id, point, size * 0.75f, 
                             markerColor.r, markerColor.g, markerColor.b, markerColor.a,
                             pulsePeriod, pulseFraction, rotateRate, bEnableCollision);
    
    // Restore original model
    if (savedCone && clumpArray) {
        clumpArray[MARKER3D_CONE] = savedCone;
        clumpArray[MARKER3D_CONE_NO_COLLISION] = savedConeNoColl;
    }
    
    // Find nearest door for rotation alignment
    CEntity* doorEntity = CEntryExitManager::FindNearestDoor(*enex, 10.0f);
    
    float angle = 0.0f;
    if (doorEntity) {
        float doorHeading = doorEntity->GetHeading();
        
        // Calculate door's forward direction
        float doorForwardX = -sin(doorHeading);
        float doorForwardY = cos(doorHeading);
        
        CVector doorPos = doorEntity->GetPosition();
        
        // Direction from door to ENEX marker
        float toEnexX = point.x - doorPos.x;
        float toEnexY = point.y - doorPos.y;
        
        // Dot product to check which side of the door
        float dot = doorForwardX * toEnexX + doorForwardY * toEnexY;
        
        angle = doorHeading;
        if (dot < 0.0f) {
            angle += PI;  // Flip if on the back side
        }
    }
    
    // Find the marker we just placed by ID
    C3dMarker* marker = nullptr;
    for (int i = 0; i < 32; i++) {
        if (C3dMarkers::m_aMarkerArray[i].m_nIdentifier == static_cast<int>(id)) {
            marker = &C3dMarkers::m_aMarkerArray[i];
            break;
        }
    }
    
    if (marker && doorEntity) {
        // Save position
        float px = marker->m_mat.pos.x;
        float py = marker->m_mat.pos.y;
        float pz = marker->m_mat.pos.z;
        
        // Counter bobbing animation (keep 30%)
        float angleRad = C3dMarkers::m_angleDiamond * (PI / 180.0f);
        float bobOffset = sin(angleRad) * 0.3f;
        pz -= bobOffset * 0.70f;
        pz -= 0.3f;  // Lower the marker
        
        // Set rotation to match door heading
        marker->m_mat.SetUnity();
        marker->m_mat.RotateZ(angle);
        
        // Restore position
        marker->m_mat.pos.x = px;
        marker->m_mat.pos.y = py;
        marker->m_mat.pos.z = pz;
        
        marker->m_mat.UpdateRW();
    }
}

// ============================================================================
// CLEANUP (for game restart/new game)
// ============================================================================

static void ClearEnexCustomData() {
    // Note: Custom clumps are managed by C3dMarkers system, don't manually free
    g_EnexCustomData.clear();
    g_DefaultCustomClump = nullptr;
    g_OriginalConeClump = nullptr;
    g_OriginalConeNoCollClump = nullptr;
    g_ModelInitialized = false;
}

// ============================================================================
// INSTALL HOOKS
// ============================================================================

void InstallHooks() {
    // Hook PlaceMarkerCone call from CEntryExitManager::Update
    PlaceMarkerCone_Original = reinterpret_cast<PlaceMarkerCone_t>(0x726D40);
    patch::RedirectCall(0x440F4E, PlaceMarkerCone_Hook);
    
    // Hook LoadEntryExit to parse extended ENEX format
    LoadEntryExit_Original = reinterpret_cast<LoadEntryExit_t>(0x5B8030);
    patch::RedirectCall(0x5B8980, LoadEntryExit_Hook);
    
    // Clear custom data before game re-initialization (new game/load game)
    // reInitGameEvent fires after CGame::ReInitGameObjectVariables which clears ENEX pool
    Events::reInitGameEvent.before += ClearEnexCustomData;
}

} // namespace Markers
