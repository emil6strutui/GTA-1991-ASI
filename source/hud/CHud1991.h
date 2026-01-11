#pragma once

// ============================================================================
// HUD 1991
// Custom HUD rendering hooks
// ============================================================================

#include <common.h>

namespace CHud1991 {

// ============================================================================
// COLORS
// ============================================================================

extern CRGBA HealthFG;
extern CRGBA HealthBG;
extern CRGBA ArmorFG;
extern CRGBA ArmorBG;
extern CRGBA BreathFG;
extern CRGBA BreathBG;
extern CRGBA BorderColor;

// ============================================================================
// PUBLIC INTERFACE
// ============================================================================

void InstallHooks();

} // namespace CHud1991
