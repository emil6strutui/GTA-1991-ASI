#pragma once

#include <common.h>

// ============================================================================
// HUD ELEMENTS
// Custom rendering for HUD components
// ============================================================================

namespace HudElements {

// Initialize all hooks
void InstallHooks();

// --- Colors ---
extern CRGBA HealthFG;
extern CRGBA HealthBG;
extern CRGBA ArmorFG;
extern CRGBA ArmorBG;
extern CRGBA BreathFG;
extern CRGBA BreathBG;
extern CRGBA BorderColor;

} // namespace HudElements

