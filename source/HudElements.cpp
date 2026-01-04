#include "HudElements.h"
#include "HudLayout.h"
#include "Drawing.h"

#include <plugin.h>
#include <CHud.h>
#include <CWorld.h>
#include <CPlayerPed.h>
#include <CPlayerInfo.h>
#include <CStats.h>
#include <CTimer.h>
#include <CFont.h>

using namespace plugin;

namespace HudElements {

// ============================================================================
// COLORS
// ============================================================================

CRGBA HealthFG(0, 104, 131, 255);
CRGBA HealthBG(0, 52, 54, 180);
CRGBA ArmorFG(225, 225, 225, 255);
CRGBA ArmorBG(112, 112, 112, 180);
CRGBA BreathFG(172, 142, 104, 255);
CRGBA BreathBG(86, 71, 52, 180);
CRGBA BorderColor(0, 0, 0, 255);

// ============================================================================
// ROUNDED BAR DRAWING
// ============================================================================

static constexpr int SEGMENTS = 6;
static constexpr int CAP_SLICES = 8;

static void DrawRoundedBar(float x, float y, float width, float height, 
                           float fillPercent, CRGBA fgColor, CRGBA bgColor) {
    float radius = height / 2.0f;
    float bodyWidth = width - height;
    
    if (fillPercent < 0.0f) fillPercent = 0.0f;
    if (fillPercent > 100.0f) fillPercent = 100.0f;
    
    // Background (pill shape)
    Drawing::Semicircle(x + radius, y + radius, radius, PI * 0.5f, PI * 1.5f, SEGMENTS, bgColor);
    if (bodyWidth > 0) {
        Drawing::FilledRect(x + radius, y, bodyWidth, height, bgColor);
    }
    Drawing::Semicircle(x + width - radius, y + radius, radius, -PI * 0.5f, PI * 0.5f, SEGMENTS, bgColor);
    
    // Foreground fill
    if (fillPercent > 0) {
        float fillWidth = width * (fillPercent / 100.0f);
        
        // Left cap (smooth curve-following fill)
        if (fillWidth > 0) {
            float leftCapFill = (fillWidth >= radius) ? radius : fillWidth;
            float capCenterX = x + radius;
            float centerY = y + radius;
            Drawing::PartialLeftCapFill(capCenterX, centerY, radius, leftCapFill, CAP_SLICES, fgColor);
        }
        
        // Center body
        if (fillWidth > radius && bodyWidth > 0) {
            float bodyFill = fillWidth - radius;
            if (bodyFill > bodyWidth) bodyFill = bodyWidth;
            Drawing::FilledRect(x + radius, y, bodyFill, height, fgColor);
        }
        
        // Right cap (smooth curve-following fill)
        if (fillWidth > width - radius) {
            float rightFillWidth = fillWidth - (width - radius);
            float capCenterX = x + width - radius;
            float centerY = y + radius;
            Drawing::PartialRightCapFill(capCenterX, centerY, radius, rightFillWidth, CAP_SLICES, fgColor);
        }
    }
}

static void DrawRoundedBarWithBorder(float x, float y, float width, float height, 
                                     float fillPercent, CRGBA fgColor, CRGBA bgColor,
                                     float borderWidth, CRGBA borderColor) {
    DrawRoundedBar(x - borderWidth, y - borderWidth, 
                   width + borderWidth * 2.0f, height + borderWidth * 2.0f,
                   100.0f, borderColor, borderColor);
    DrawRoundedBar(x, y, width, height, fillPercent, fgColor, bgColor);
}

// ============================================================================
// HOOK FUNCTIONS
// ============================================================================

// Clock PrintString hook
void __cdecl ClockPrintString_Hook(float /*x*/, float /*y*/, char* text) {
    if (!g_HudLayout.showClock) return;
    CFont::PrintString(Screen::FromRight(g_HudLayout.rightMargin), Screen::StretchY(g_HudLayout.clockY), text);
}

// Money PrintString hook
void __cdecl MoneyPrintString_Hook(float /*x*/, float /*y*/, char* text) {
    if (!g_HudLayout.showMoney) return;
    CFont::PrintString(Screen::FromRight(g_HudLayout.rightMargin), Screen::StretchY(g_HudLayout.moneyY), text);
}

// Weapon icon hook
void __cdecl WeaponIcon_Hook(CPed* ped, int /*x*/, int /*y*/, float alpha) {
    if (!g_HudLayout.showWeapon || !ped) return;
    
    int myX = static_cast<int>(Screen::FromRight(g_HudLayout.weaponX + g_HudLayout.weaponWidth));
    int myY = static_cast<int>(Screen::StretchY(g_HudLayout.weaponY));
    
    using DrawWeaponIcon_t = void(__cdecl*)(CPed*, int, int, float);
    ((DrawWeaponIcon_t)0x58D7D0)(ped, myX, myY, alpha);
}

// Ammo hook
void __cdecl Ammo_Hook(CPed* ped, int /*x*/, int /*y*/, float alpha) {
    if (!g_HudLayout.showAmmo || !ped) return;
    
    int myX = static_cast<int>(Screen::FromRight(g_HudLayout.weaponX + g_HudLayout.weaponWidth / 2.0f));
    int myY = static_cast<int>(Screen::StretchY(g_HudLayout.ammoY));
    
    using DrawAmmo_t = void(__cdecl*)(CPed*, int, int, float);
    ((DrawAmmo_t)0x5893B0)(ped, myX, myY, alpha);
}

// Health bar
void __cdecl RenderHealthBar(int playerId, int /*x*/, int /*y*/) {
    if (!g_HudLayout.showHealthBar) return;
    if (CHud::m_ItemToFlash == 4 && (CTimer::m_FrameCounter & 8)) return;
    
    CPlayerPed* player = FindPlayerPed(playerId);
    if (!player) return;
    
    float health = player->m_fHealth;
    if (health < 10.0f && (CTimer::m_FrameCounter & 8)) return;
    
    CPlayerInfo& info = CWorld::Players[playerId];
    float maxHealth = static_cast<float>(info.m_nMaxHealth);
    if (maxHealth <= 0) maxHealth = 100.0f;
    
    float barWidth = Screen::StretchY(g_HudLayout.barWidth);
    float barHeight = Screen::StretchY(g_HudLayout.barHeight);
    float barX = Screen::GetBarX(barWidth);
    float barY = Screen::StretchY(g_HudLayout.healthBarY);
    float percent = (health / maxHealth) * 100.0f;
    float border = Screen::StretchY(1.5f);
    
    DrawRoundedBarWithBorder(barX, barY, barWidth, barHeight, percent, 
                             HealthFG, HealthBG, border, BorderColor);
}

// Armor bar
void __cdecl RenderArmorBar(int playerId, int /*x*/, int /*y*/) {
    if (!g_HudLayout.showArmorBar) return;
    
    CPlayerPed* player = FindPlayerPed(playerId);
    if (!player) return;
    if ((CHud::m_ItemToFlash == 3 && (CTimer::m_FrameCounter & 8)) || player->m_fArmour <= 1.0f) return;

    CPlayerInfo& info = CWorld::Players[playerId];
    float maxArmor = static_cast<float>(info.m_nMaxArmour);
    if (maxArmor <= 0) maxArmor = 100.0f;

    float barWidth = Screen::StretchY(g_HudLayout.barWidth);
    float barHeight = Screen::StretchY(g_HudLayout.barHeight);
    float barX = Screen::GetBarX(barWidth);
    float barY = Screen::StretchY(g_HudLayout.armorBarY);
    float percent = (player->m_fArmour / maxArmor) * 100.0f;
    float border = Screen::StretchY(1.5f);

    DrawRoundedBarWithBorder(barX, barY, barWidth, barHeight, percent,
                             ArmorFG, ArmorBG, border, BorderColor);
}

// Breath bar
void __cdecl RenderBreathBar(int playerId, int /*x*/, int /*y*/) {
    if (!g_HudLayout.showBreathBar) return;
    if (CHud::m_ItemToFlash == 5 && (CTimer::m_FrameCounter & 8)) return;
    
    CPlayerPed* player = FindPlayerPed(playerId);
    if (!player || !player->m_pPlayerData) return;
    
    float breath = player->m_pPlayerData->m_fBreath;
    float maxBreath = CStats::GetFatAndMuscleModifier(STAT_MOD_AIR_IN_LUNG);
    if (maxBreath <= 0) maxBreath = 100.0f;
    
    float barWidth = Screen::StretchY(g_HudLayout.barWidth);
    float barHeight = Screen::StretchY(g_HudLayout.barHeight);
    float barX = Screen::GetBarX(barWidth);
    float barY = Screen::StretchY(g_HudLayout.breathBarY);
    float percent = (breath / maxBreath) * 100.0f;
    float border = Screen::StretchY(1.5f);
    
    DrawRoundedBarWithBorder(barX, barY, barWidth, barHeight, percent,
                             BreathFG, BreathBG, border, BorderColor);
}

// ============================================================================
// INSTALL HOOKS
// ============================================================================

void InstallHooks() {
    // Clock & Money text
    patch::RedirectCall(0x58EC21, ClockPrintString_Hook);
    patch::RedirectCall(0x58F607, MoneyPrintString_Hook);
    
    // Weapon icon (P1 & P2)
    patch::RedirectCall(0x58F944, WeaponIcon_Hook);
    patch::RedirectCall(0x58F9B1, WeaponIcon_Hook);
    
    // Ammo (P1 & P2)
    patch::RedirectCall(0x58FA25, Ammo_Hook);
    patch::RedirectCall(0x58FABE, Ammo_Hook);
    
    // Bars
    patch::RedirectJump(0x589270, RenderHealthBar);
    patch::RedirectJump(0x5890A0, RenderArmorBar);
    patch::RedirectJump(0x589190, RenderBreathBar);
}

} // namespace HudElements

