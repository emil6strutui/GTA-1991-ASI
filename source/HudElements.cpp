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
#include <CHudColours.h>
#include <CWanted.h>

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

static constexpr int SEGMENTS = 11;
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

        // Left cap
        if (fillWidth > 0) {
            if (fillWidth >= radius * 0.99f) {
                // Full cap - use Semicircle to avoid sharp tip artifact
                Drawing::Semicircle(x + radius, y + radius, radius, PI * 0.5f, PI * 1.5f, SEGMENTS, fgColor);
            } else {
                // Partial cap - use slices (doesn't reach the sharp tip)
                Drawing::PartialLeftCapFill(x + radius, y + radius, radius, fillWidth, CAP_SLICES, fgColor);
            }
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

void __cdecl RenderWantedStars() {
    CWanted* wanted = FindPlayerWanted(-1);
    if (!wanted) return;
    
    int wantedLevel = wanted->m_nWantedLevel;
    int wantedBeforeParole = wanted->m_nWantedLevelBeforeParole;
    
    // State machine for fade in/out (using CHud statics)
    static int& m_WantedState = *(int*)0xBAA400;
    static int& m_WantedTimer = *(int*)0xBAA404;
    static int& m_WantedFadeTimer = *(int*)0xBAA408;
    static int& m_LastWanted = *(int*)0xBAA40C;
    static bool& byte_BAB228 = *(bool*)0xBAB228;
    
    float alpha = 255.0f;
    
    // Fade state machine (exact copy of original logic)
    if (m_LastWanted != wantedLevel) {
        if (m_WantedState == 0) {
            m_WantedFadeTimer = 0;
        }
        m_WantedTimer = 5;
        m_WantedState = 2;  // Start fade in
    }
    
    if (m_WantedState) {
        switch (m_WantedState) {
            case 1:  // Fully visible
                m_WantedFadeTimer = 1000;
                alpha = 255.0f;
                if (m_WantedTimer > 10000) {
                    m_WantedState = 3;  // Start fade out
                    m_WantedFadeTimer = 3000;
                }
                m_WantedTimer += static_cast<int>(CTimer::ms_fTimeStep * 20.0f);
                break;
                
            case 2:  // Fading in
                m_WantedFadeTimer += static_cast<int>(CTimer::ms_fTimeStep * 20.0f);
                if (m_WantedFadeTimer > 1000) {
                    m_WantedFadeTimer = 1000;
                    m_WantedState = 1;
                }
                alpha = m_WantedFadeTimer * 0.255f;
                m_WantedTimer += static_cast<int>(CTimer::ms_fTimeStep * 20.0f);
                break;
                
            case 3:  // Fading out
                m_WantedFadeTimer -= static_cast<int>(CTimer::ms_fTimeStep * 20.0f);
                if (m_WantedFadeTimer < 0) {
                    m_WantedFadeTimer = 0;
                    m_WantedState = 0;
                }
                alpha = m_WantedFadeTimer * 0.255f;
                m_WantedTimer += static_cast<int>(CTimer::ms_fTimeStep * 20.0f);
                break;
        }
        byte_BAB228 = (m_WantedState == 1);
    }
    
    alpha = std::clamp(alpha, 0.0f, 255.0f);
    m_LastWanted = wantedLevel;
    
    // Only draw if visible
    if (!g_HudLayout.showStars) return;
    if (m_WantedState == 0) return;
    if (wantedLevel <= 0 && !byte_BAB228 && wantedBeforeParole <= 0) return;
    
    // Setup font
    CFont::SetBackground(false, false);
    CFont::SetScale(
        Screen::StretchY(g_HudLayout.starScaleX),  // Use StretchY for both to maintain proportions
        Screen::StretchY(g_HudLayout.starScaleY)
    );
    CFont::SetOrientation(eFontAlignment::ALIGN_LEFT);
    CFont::SetProportional(true);
    CFont::SetFontStyle(eFontStyle::FONT_GOTHIC);
    
    char starText[4];
    strcpy(starText, "]");
    char gxtStar[8];
    AsciiToGxtChar(starText, gxtStar);
    
    // Position (Y-based scaling for consistent positioning regardless of aspect ratio)
    float startX = Screen::StretchY(g_HudLayout.starsX);
    float y = Screen::StretchY(g_HudLayout.starsY);
    float spacing = Screen::StretchY(g_HudLayout.starSpacing);
    
    unsigned char alphaChar = static_cast<unsigned char>(alpha);
    
    // Draw 6 stars from LEFT to RIGHT
    for (int i = 0; i < 6; i++) {
        float x = startX + (i * spacing);
        
        CFont::SetEdge(1);
        CFont::SetDropColor(CRGBA(0, 0, 0, alphaChar));
        CFont::SetScale(
            Screen::StretchY(g_HudLayout.starScaleX),  // Use StretchY for both to maintain proportions
            Screen::StretchY(g_HudLayout.starScaleY)
        );
        
        // Check if this star should be filled (gold)
        bool shouldFlash = (CTimer::m_FrameCounter & 4) != 0;
        uint32_t timeSinceChange = CTimer::m_snTimeInMilliseconds - wanted->m_nLastTimeWantedLevelChanged;
        
        if (i < wantedLevel && (timeSinceChange > 2000 || shouldFlash)) {
            // Filled star (gold/orange)
            CFont::SetColor(HudColour.GetRGB(HUD_COLOUR_REDDARK, alphaChar));
            CFont::PrintString(x, y, gxtStar);
        }
        else if (i < wantedBeforeParole && shouldFlash) {
            // Parole star (dimmed, flashing)
            CRGBA baseColor = HudColour.GetRGB(HUD_COLOUR_REDDARK, 255);
            CRGBA dimmed(
                static_cast<unsigned char>(baseColor.r * 0.8f),
                static_cast<unsigned char>(baseColor.g * 0.8f),
                static_cast<unsigned char>(baseColor.b * 0.8f),
                alphaChar
            );
            CFont::SetColor(dimmed);
            CFont::PrintString(x, y, gxtStar);
        }
        else if (i >= wantedLevel) {
            // Empty star (dark, no edge, slightly larger)
            CFont::SetEdge(0);
            CFont::SetColor(CRGBA(0, 0, 0, static_cast<unsigned char>(alpha * 0.7f)));
            CFont::SetScale(
                Screen::StretchY(g_HudLayout.starScaleX * g_HudLayout.starEmptyScale),  // StretchY for proportional scaling
                Screen::StretchY(g_HudLayout.starScaleY * g_HudLayout.starEmptyScale)
            );
            CFont::PrintString(x, y - Screen::StretchY(g_HudLayout.starEmptyYOffset), gxtStar);
        }
    }
    
    CFont::SetEdge(0);
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
    //patch::RedirectCall(0x58F9B1, WeaponIcon_Hook);
    
    // Ammo (P1 & P2)
    patch::RedirectCall(0x58FA25, Ammo_Hook);
    //patch::RedirectCall(0x58FABE, Ammo_Hook);
    
    // Bars
    patch::RedirectJump(0x589270, RenderHealthBar);
    patch::RedirectJump(0x5890A0, RenderArmorBar);
    patch::RedirectJump(0x589190, RenderBreathBar);
    
    // Wanted stars
    patch::RedirectJump(0x58D9A0, RenderWantedStars);
    
    // Weapon icon size - use SetPointer to avoid affecting cross-references
    // fmul ds:flt_XXXXXX instruction: D8 0D [4-byte address] - address operand is at offset +2
    static float weaponWidth = g_HudLayout.weaponWidth;
    static float weaponHeight = g_HudLayout.weaponHeight;
    
    // Width (flt_866C4C) - redirect both uses in DrawWeaponIcon
    patch::SetPointer(0x58D8C9 + 2, &weaponWidth);  // Main branch
    patch::SetPointer(0x58D933 + 2, &weaponWidth);  // Fallback branch
    
    // Height (flt_866C50) - redirect both uses in DrawWeaponIcon
    patch::SetPointer(0x58D894 + 2, &weaponHeight); // Main branch
    patch::SetPointer(0x58D94B + 2, &weaponHeight); // Fallback branch
}

} // namespace HudElements

