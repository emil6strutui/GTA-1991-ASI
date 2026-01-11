#include "plugin.h"
#include "CHud1991.h"
#include "CHudLayout1991.h"
#include "CDrawing1991.h"

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

CHudLayout1991 HudLayout;

namespace CHud1991 {

// Returns Y position for bar at given slot (0 = first, 1 = second, etc.)
static float GetBarSlotY(int slot) {
    float totalHeight = HudLayout.barHeight + HudLayout.barBorderWidth * 2;
    return HudLayout.barStartY + slot * (totalHeight + HudLayout.barSpacing);
}


static void DrawRoundedBar(float x, float y, float w, float h, float percent,
                           CRGBA fg, CRGBA bg) {
    float r = h / 2.0f;
    float body = w - h;

    percent = std::clamp(percent, 0.0f, 100.0f);

    Drawing::SetupRenderState();

    // Background pill
    Drawing::Semicircle(x + r, y + r, r, PI * 0.5f, PI * 1.5f, HudLayout.barSegments, bg);
    if (body > 0) Drawing::FilledRect(x + r, y, body, h, bg);
    Drawing::Semicircle(x + w - r, y + r, r, -PI * 0.5f, PI * 0.5f, HudLayout.barSegments, bg);

    // Foreground fill
    if (percent > 0) {
        float fill = w * (percent / 100.0f);

        // Left cap
        if (fill >= r * 0.99f) {
            Drawing::Semicircle(x + r, y + r, r, PI * 0.5f, PI * 1.5f, HudLayout.barSegments, fg);
        } else if (fill > 0) {
            Drawing::PartialLeftCapFill(x + r, y + r, r, fill, HudLayout.barCapSlices, fg);
        }

        // Body
        if (fill > r && body > 0) {
            float bodyFill = std::min(fill - r, body);
            Drawing::FilledRect(x + r, y, bodyFill, h, fg);
        }

        // Right cap
        if (fill > w - r) {
            Drawing::PartialRightCapFill(x + w - r, y + r, r, fill - (w - r), HudLayout.barCapSlices, fg);
        }
    }
}

static void DrawRoundedBarWithBorder(float x, float y, float w, float h, float percent,
                                      CRGBA fg, CRGBA bg, float border, CRGBA borderCol) {
                                        
    DrawRoundedBar(x - border, y - border, w + border * 2, h + border * 2, 100.0f, borderCol, borderCol);
    DrawRoundedBar(x, y, w, h, percent, fg, bg);
}

static void __cdecl DrawClock(float, float, char* text) {
    if (!HudLayout.showClock) return;
    CFont::PrintString(Screen::FromRight(HudLayout.rightMargin), Screen::StretchY(HudLayout.clockY), text);
}

static void __cdecl DrawMoney(float, float, char* text) {
    if (!HudLayout.showMoney) return;
    CFont::PrintString(Screen::FromRight(HudLayout.rightMargin), Screen::StretchY(HudLayout.moneyY), text);
}

static void __cdecl DrawWeaponIcon(CPed* ped, int, int, float alpha) {
    if (!HudLayout.showWeapon || !ped) return;

    int x = static_cast<int>(Screen::FromRight(HudLayout.weaponX + HudLayout.weaponWidth));
    int y = static_cast<int>(Screen::StretchY(HudLayout.weaponY));

    reinterpret_cast<void(__cdecl*)(CPed*, int, int, float)>(0x58D7D0)(ped, x, y, alpha);
}

static void __cdecl DrawAmmo(CPed* ped, int, int, float alpha) {
    if (!HudLayout.showAmmo || !ped) return;

    int x = static_cast<int>(Screen::FromRight(HudLayout.weaponX + HudLayout.weaponWidth / 2.0f));
    int y = static_cast<int>(Screen::StretchY(HudLayout.ammoY));

    reinterpret_cast<void(__cdecl*)(CPed*, int, int, float)>(0x5893B0)(ped, x, y, alpha);
}

static void __cdecl DrawHealthBar(int playerId, int, int) {
    if (!HudLayout.showHealthBar) return;
    if (CHud::m_ItemToFlash == 4 && (CTimer::m_FrameCounter & 8)) return;

    CPlayerPed* player = FindPlayerPed(playerId);
    if (!player) return;

    float health = player->m_fHealth;
    if (health < 10.0f && (CTimer::m_FrameCounter & 8)) return;

    float maxHealth = static_cast<float>(CWorld::Players[playerId].m_nMaxHealth);
    if (maxHealth <= 0) maxHealth = 100.0f;

    float w = Screen::StretchY(HudLayout.barWidth);
    float h = Screen::StretchY(HudLayout.barHeight);
    float x = Screen::GetBarX(w);
    float y = Screen::StretchY(GetBarSlotY(0));  // Always slot 0
    float border = Screen::StretchY(HudLayout.barBorderWidth);

    DrawRoundedBarWithBorder(x, y, w, h, (health / maxHealth) * 100.0f,
                              HudLayout.healthFG, HudLayout.healthBG, border, HudLayout.barBorderColor);
}

static void __cdecl DrawArmorBar(int playerId, int, int) {
    if (!HudLayout.showArmorBar) return;

    CPlayerPed* player = FindPlayerPed(playerId);
    if (!player) return;
    if ((CHud::m_ItemToFlash == 3 && (CTimer::m_FrameCounter & 8)) || player->m_fArmour <= 1.0f) return;

    float maxArmor = static_cast<float>(CWorld::Players[playerId].m_nMaxArmour);
    if (maxArmor <= 0) maxArmor = 100.0f;

    float w = Screen::StretchY(HudLayout.barWidth);
    float h = Screen::StretchY(HudLayout.barHeight);
    float x = Screen::GetBarX(w);
    float y = Screen::StretchY(GetBarSlotY(1));  // Slot 1 (after health)
    float border = Screen::StretchY(HudLayout.barBorderWidth);

    DrawRoundedBarWithBorder(x, y, w, h, (player->m_fArmour / maxArmor) * 100.0f,
                              HudLayout.armorFG, HudLayout.armorBG, border, HudLayout.barBorderColor);
}

static void __cdecl DrawBreathBar(int playerId, int, int) {
    if (!HudLayout.showBreathBar) return;
    if (CHud::m_ItemToFlash == 5 && (CTimer::m_FrameCounter & 8)) return;

    CPlayerPed* player = FindPlayerPed(playerId);
    if (!player || !player->m_pPlayerData) return;

    float breath = player->m_pPlayerData->m_fBreath;
    float maxBreath = CStats::GetFatAndMuscleModifier(STAT_MOD_AIR_IN_LUNG);
    if (maxBreath <= 0) maxBreath = 100.0f;

    // Slot 2 if armor visible, slot 1 if not
    int slot = (player->m_fArmour > 0.0f) ? 2 : 1;

    float w = Screen::StretchY(HudLayout.barWidth);
    float h = Screen::StretchY(HudLayout.barHeight);
    float x = Screen::GetBarX(w);
    float y = Screen::StretchY(GetBarSlotY(slot));
    float border = Screen::StretchY(HudLayout.barBorderWidth);

    DrawRoundedBarWithBorder(x, y, w, h, (breath / maxBreath) * 100.0f,
                              HudLayout.breathFG, HudLayout.breathBG, border, HudLayout.barBorderColor);
}

static void __cdecl DrawWantedStars() {
    CWanted* wanted = FindPlayerWanted(-1);
    if (!wanted) return;

    int level = wanted->m_nWantedLevel;
    int parole = wanted->m_nWantedLevelBeforeParole;

    // Fade state machine (game statics)
    static int& state = *(int*)0xBAA400;
    static int& timer = *(int*)0xBAA404;
    static int& fadeTimer = *(int*)0xBAA408;
    static int& lastLevel = *(int*)0xBAA40C;
    static bool& isVisible = *(bool*)0xBAB228;

    float alpha = 255.0f;

    if (lastLevel != level) {
        if (state == 0) fadeTimer = 0;
        timer = 5;
        state = 2;
    }

    if (state) {
        int step = static_cast<int>(CTimer::ms_fTimeStep * 20.0f);

        switch (state) {
            case 1: // Visible
                fadeTimer = 1000;
                alpha = 255.0f;
                if (timer > 10000) { state = 3; fadeTimer = 3000; }
                timer += step;
                break;
            case 2: // Fade in
                fadeTimer += step;
                if (fadeTimer > 1000) { fadeTimer = 1000; state = 1; }
                alpha = fadeTimer * 0.255f;
                timer += step;
                break;
            case 3: // Fade out
                fadeTimer -= step;
                if (fadeTimer < 0) { fadeTimer = 0; state = 0; }
                alpha = fadeTimer * 0.255f;
                timer += step;
                break;
        }
        isVisible = (state == 1);
    }

    alpha = std::clamp(alpha, 0.0f, 255.0f);
    lastLevel = level;

    if (!HudLayout.showStars || state == 0) return;
    if (level <= 0 && !isVisible && parole <= 0) return;

    CFont::SetBackground(false, false);
    CFont::SetScale(Screen::StretchY(HudLayout.starScaleX), Screen::StretchY(HudLayout.starScaleY));
    CFont::SetOrientation(eFontAlignment::ALIGN_LEFT);
    CFont::SetProportional(true);
    CFont::SetFontStyle(eFontStyle::FONT_GOTHIC);

    char gxtStar[8];
    AsciiToGxtChar("]", gxtStar);

    float startX = Screen::StretchY(HudLayout.starsX);
    float y = Screen::StretchY(HudLayout.starsY);
    float spacing = Screen::StretchY(HudLayout.starSpacing);
    unsigned char a = static_cast<unsigned char>(alpha);

    for (int i = 0; i < 6; i++) {
        float x = startX + i * spacing;
        bool flash = (CTimer::m_FrameCounter & 4) != 0;
        uint32_t timeSince = CTimer::m_snTimeInMilliseconds - wanted->m_nLastTimeWantedLevelChanged;

        CFont::SetEdge(1);
        CFont::SetDropColor(CRGBA(0, 0, 0, a));
        CFont::SetScale(Screen::StretchY(HudLayout.starScaleX), Screen::StretchY(HudLayout.starScaleY));

        if (i < level && (timeSince > 2000 || flash)) {
            CFont::SetColor(HudColour.GetRGB(HUD_COLOUR_REDDARK, a));
            CFont::PrintString(x, y, gxtStar);
        }
        else if (i < parole && flash) {
            CRGBA base = HudColour.GetRGB(HUD_COLOUR_REDDARK, 255);
            CFont::SetColor(CRGBA(base.r * 0.8f, base.g * 0.8f, base.b * 0.8f, a));
            CFont::PrintString(x, y, gxtStar);
        }
        else if (i >= level) {
            CFont::SetEdge(0);
            CFont::SetColor(CRGBA(0, 0, 0, static_cast<unsigned char>(alpha * 0.7f)));
            CFont::SetScale(Screen::StretchY(HudLayout.starScaleX * HudLayout.starEmptyScale),
                           Screen::StretchY(HudLayout.starScaleY * HudLayout.starEmptyScale));
            CFont::PrintString(x, y - Screen::StretchY(HudLayout.starEmptyYOffset), gxtStar);
        }
    }

    CFont::SetEdge(0);
}

void InstallHooks() {
    patch::RedirectCall(0x58EC21, DrawClock);

    static const char* fmtPos = "$%d";
    static const char* fmtNeg = "-$%d";
    patch::SetPointer(0x58F4C7 + 1, fmtPos);
    patch::SetPointer(0x58F509 + 1, fmtNeg);
    patch::RedirectCall(0x58F607, DrawMoney);

    patch::RedirectCall(0x58F944, DrawWeaponIcon);
    patch::RedirectCall(0x58FA25, DrawAmmo);

    patch::RedirectJump(0x589270, DrawHealthBar);
    patch::RedirectJump(0x5890A0, DrawArmorBar);
    patch::RedirectJump(0x589190, DrawBreathBar);

    patch::RedirectJump(0x58D9A0, DrawWantedStars);

    patch::SetPointer(0x58D894 + 2, &HudLayout.weaponHeight);  // fmul height (first)
    patch::SetPointer(0x58D8C9 + 2, &HudLayout.weaponWidth);   // fmul width (first)
    patch::SetPointer(0x58D933 + 2, &HudLayout.weaponWidth);   // fmul width (second)
    patch::SetPointer(0x58D94B + 2, &HudLayout.weaponHeight);  // fmul height (second)
}

}
