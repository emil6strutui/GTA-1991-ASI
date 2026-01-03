#include <plugin.h>
#include <CHud.h>
#include <CSprite2d.h>
#include <CWorld.h>
#include <CPlayerPed.h>
#include <CPlayerInfo.h>
#include <CStats.h>
#include <CTimer.h>
#include <RenderWare.h>
#include <common.h>
#include <skeleton.h>
#include <CFont.h>
#include <CClock.h>
#include <cmath>
#include <cstdio>

using namespace plugin;

// Screen scaling functions (reference 640x448)
inline float ScreenStretchX(float x) {
    return x * static_cast<float>(RsGlobal.maximumWidth) / 640.0f;
}
inline float ScreenStretchY(float y) {
    return y * static_cast<float>(RsGlobal.maximumHeight) / 448.0f;
}

// ============================================================================
// HUD LAYOUT CONFIGURATION  
// ============================================================================
// Complete HUD positioning system - hooks DrawPlayerInfo entirely
// Does NOT patch shared game floats (safe for map screen, etc.)

// HUD Layout settings (in 640x448 reference coordinates)
struct HudLayout {
    // Right margin from screen edge
    float rightMargin = 62.0f;
    
    // Clock position
    float clockY = 22.0f;
    
    // Money position  
    float moneyY = 43.0f;
    
    // Weapon icon position (top-right corner, relative to right edge)
    float weaponX = 62.0f;      // From right edge
    float weaponY = 20.0f;
    float weaponWidth = 47.0f;
    float weaponHeight = 58.0f;
    
    // Ammo position (below weapon icon)
    float ammoY = 78.0f;
    
    // Bar dimensions
    float barWidth = 120.0f;
    float barHeight = 9.0f;
    
    // Bar Y positions
    float healthBarY = 64.0f;
    float armorBarY = 76.0f;
    float breathBarY = 88.0f;
    
    // Visibility flags
    bool showClock = true;
    bool showMoney = true;
    bool showWeapon = true;
    bool showAmmo = true;
    bool showHealthBar = true;
    bool showArmorBar = true;
    bool showBreathBar = true;
};

// Global layout instance
static HudLayout g_HudLayout;

// === Screen Helper Functions ===

inline float ScreenFromRight(float x) {
    return static_cast<float>(RsGlobal.maximumWidth) - ScreenStretchX(x);
}

inline float GetBarRightAlignedX(float barWidth) {
    return ScreenFromRight(g_HudLayout.rightMargin) - barWidth;
}

// ============================================================================
// END HUD LAYOUT CONFIGURATION
// ============================================================================

// === Colors ===
static CRGBA HEALTH_FG(180, 25, 29, 255);     // Red
static CRGBA HEALTH_BG(90, 12, 14, 180);      // Dark red
static CRGBA ARMOR_FG(225, 225, 225, 255);    // Light gray
static CRGBA ARMOR_BG(112, 112, 112, 180);    // Dark gray
static CRGBA BORDER_COLOR(0, 0, 0, 255);      // Black border

// === Drawing Helpers ===

void SetupRenderState() {
    RwRenderStateSet(rwRENDERSTATETEXTURERASTER, nullptr);
    RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
    RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
    RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
}

void SetVertex(RwIm2DVertex& vtx, float x, float y, CRGBA color) {
    RwIm2DVertexSetScreenX(&vtx, x);
    RwIm2DVertexSetScreenY(&vtx, y);
    RwIm2DVertexSetScreenZ(&vtx, CSprite2d::NearScreenZ);
    RwIm2DVertexSetRecipCameraZ(&vtx, CSprite2d::RecipNearClip);
    RwIm2DVertexSetU(&vtx, 0.0f, CSprite2d::RecipNearClip);
    RwIm2DVertexSetV(&vtx, 0.0f, CSprite2d::RecipNearClip);
    RwIm2DVertexSetIntRGBA(&vtx, color.r, color.g, color.b, color.a);
}

void DrawSemicircle(float cx, float cy, float radius, float startAngle, float endAngle, 
                    int segments, CRGBA color) {
    SetupRenderState();
    
    RwIm2DVertex verts[8]; // Max 8 vertices
    int numVerts = (segments + 2 < 8) ? (segments + 2) : 8;
    
    // Center vertex
    SetVertex(verts[0], cx, cy, color);
    
    // Arc vertices
    float angleStep = (endAngle - startAngle) / segments;
    for (int i = 0; i <= segments && i < 7; i++) {
        float angle = startAngle + i * angleStep;
        float x = cx + cosf(angle) * radius;
        float y = cy + sinf(angle) * radius;
        SetVertex(verts[i + 1], x, y, color);
    }
    
    RwIm2DRenderPrimitive(rwPRIMTYPETRIFAN, verts, numVerts);
}

void DrawFilledRect(float x, float y, float w, float h, CRGBA color) {
    SetupRenderState();
    
    RwIm2DVertex verts[4];
    SetVertex(verts[0], x, y + h, color);       // bottom-left
    SetVertex(verts[1], x + w, y + h, color);   // bottom-right
    SetVertex(verts[2], x + w, y, color);       // top-right
    SetVertex(verts[3], x, y, color);           // top-left
    
    RwIm2DRenderPrimitive(rwPRIMTYPETRIFAN, verts, 4);
}

// Draw a partial right cap fill using vertical slices that follow the curve
// This creates a smooth curved edge without artifacts
void DrawPartialRightCapFill(float capCenterX, float capCenterY, float radius, 
                              float fillWidth, int numSlices, CRGBA color) {
    if (fillWidth <= 0 || numSlices < 1) return;
    
    SetupRenderState();
    
    float sliceWidth = fillWidth / numSlices;
    
    for (int i = 0; i < numSlices; i++) {
        float x1 = capCenterX + i * sliceWidth;
        float x2 = x1 + sliceWidth;
        
        // Calculate height at each x position using circle equation
        float dx1 = i * sliceWidth;
        float dx2 = (i + 1) * sliceWidth;
        
        // Clamp to radius to avoid sqrt of negative
        if (dx1 > radius) dx1 = radius;
        if (dx2 > radius) dx2 = radius;
        
        float h1 = sqrtf(radius * radius - dx1 * dx1);
        float h2 = sqrtf(radius * radius - dx2 * dx2);
        
        RwIm2DVertex verts[4];
        SetVertex(verts[0], x1, capCenterY + h1, color);  // bottom-left
        SetVertex(verts[1], x2, capCenterY + h2, color);  // bottom-right
        SetVertex(verts[2], x2, capCenterY - h2, color);  // top-right
        SetVertex(verts[3], x1, capCenterY - h1, color);  // top-left
        
        RwIm2DRenderPrimitive(rwPRIMTYPETRIFAN, verts, 4);
    }
}

void DrawRoundedBar(float x, float y, float width, float height, 
                    float fillPercent, CRGBA fgColor, CRGBA bgColor) {
    float radius = height / 2.0f;  // Full height semicircles
    float bodyWidth = width - height;  // Center section between caps
    const int SEGMENTS = 6;
    
    if (fillPercent < 0.0f) fillPercent = 0.0f;
    if (fillPercent > 100.0f) fillPercent = 100.0f;
    
    // === Draw full background (pill shape) ===
    DrawSemicircle(x + radius, y + radius, radius, PI * 0.5f, PI * 1.5f, SEGMENTS, bgColor);
    if (bodyWidth > 0) {
        DrawFilledRect(x + radius, y, bodyWidth, height, bgColor);
    }
    DrawSemicircle(x + width - radius, y + radius, radius, -PI * 0.5f, PI * 0.5f, SEGMENTS, bgColor);
    
    // === Draw foreground fill with progressive cap filling ===
    if (fillPercent > 0) {
        float fillWidth = width * (fillPercent / 100.0f);
        
        // LEFT CAP - spreads from leftmost point (PI) toward top/bottom
        if (fillWidth > 0) {
            if (fillWidth >= radius) {
                // Full left cap
                DrawSemicircle(x + radius, y + radius, radius, PI * 0.5f, PI * 1.5f, SEGMENTS, fgColor);
            } else {
                // Partial left cap
                float fillRatio = fillWidth / radius;
                float halfArc = (PI * 0.5f) * fillRatio;
                DrawSemicircle(x + radius, y + radius, radius, PI - halfArc, PI + halfArc, SEGMENTS, fgColor);
            }
        }
        
        // CENTER BODY - fills progressively
        if (fillWidth > radius && bodyWidth > 0) {
            float bodyFill = fillWidth - radius;
            if (bodyFill > bodyWidth) bodyFill = bodyWidth;
            DrawFilledRect(x + radius, y, bodyFill, height, fgColor);
        }
        
        // RIGHT CAP - smooth fill using vertical slices that follow the curve
        if (fillWidth > width - radius) {
            float rightFillWidth = fillWidth - (width - radius);
            float capCenterX = x + width - radius;
            float centerY = y + radius;
            
            // Use 8 slices for smooth curve - more segments = smoother but more draw calls
            DrawPartialRightCapFill(capCenterX, centerY, radius, rightFillWidth, 8, fgColor);
        }
    }
}

// Draw rounded bar with border
void DrawRoundedBarWithBorder(float x, float y, float width, float height, 
                              float fillPercent, CRGBA fgColor, CRGBA bgColor,
                              float borderWidth, CRGBA borderColor) {
    // Draw border (slightly larger bar behind)
    DrawRoundedBar(x - borderWidth, y - borderWidth, 
                   width + borderWidth * 2.0f, height + borderWidth * 2.0f,
                   100.0f, borderColor, borderColor);
    
    // Draw main bar on top
    DrawRoundedBar(x, y, width, height, fillPercent, fgColor, bgColor);
}

// ============================================================================
// CALL REDIRECT HOOKS
// These intercept the original draw calls inside DrawPlayerInfo
// The original state machines still run - we just change WHERE things draw
// ============================================================================

// === Clock PrintString Hook ===
// Redirects: 0x58EC21 (CFont::PrintString for clock)
// Original receives text already formatted, font settings already applied
void __cdecl ClockPrintString_Hook(float /*x*/, float /*y*/, char* text) {
    if (!g_HudLayout.showClock) return;
    // Font is already set up by original code, just print at our position
    CFont::PrintString(ScreenFromRight(g_HudLayout.rightMargin), ScreenStretchY(g_HudLayout.clockY), text);
}

// === Money PrintString Hook ===
// Redirects: 0x58F607 (CFont::PrintString for money)
void __cdecl MoneyPrintString_Hook(float /*x*/, float /*y*/, char* text) {
    if (!g_HudLayout.showMoney) return;
    // Font is already set up by original code, just print at our position
    CFont::PrintString(ScreenFromRight(g_HudLayout.rightMargin), ScreenStretchY(g_HudLayout.moneyY), text);
}

// === Weapon Icon Hook ===
// Redirects: 0x58F944 (Player 1), 0x58F9B1 (Player 2)
// Signature: void __cdecl CHud::DrawWeaponIcon(CPed* ped, int x, int y, float alpha)
void __cdecl WeaponIcon_Hook(CPed* ped, int /*x*/, int /*y*/, float alpha) {
    if (!g_HudLayout.showWeapon || !ped) return;
    
    int myX = static_cast<int>(ScreenFromRight(g_HudLayout.weaponX + g_HudLayout.weaponWidth));
    int myY = static_cast<int>(ScreenStretchY(g_HudLayout.weaponY));
    
    // Call original DrawWeaponIcon at 0x58D7D0
    using DrawWeaponIcon_t = void(__cdecl*)(CPed*, int, int, float);
    ((DrawWeaponIcon_t)0x58D7D0)(ped, myX, myY, alpha);
}

// === Ammo Hook ===
// Redirects: 0x58FA25 (Player 1), 0x58FABE (Player 2)
// Signature: void __cdecl CHud::DrawAmmo(CPed* ped, int x, int y, float alpha)
void __cdecl Ammo_Hook(CPed* ped, int /*x*/, int /*y*/, float alpha) {
    if (!g_HudLayout.showAmmo || !ped) return;
    
    int myX = static_cast<int>(ScreenFromRight(g_HudLayout.weaponX + g_HudLayout.weaponWidth / 2.0f));
    int myY = static_cast<int>(ScreenStretchY(g_HudLayout.ammoY));
    
    // Call original DrawAmmo at 0x5893B0
    using DrawAmmo_t = void(__cdecl*)(CPed*, int, int, float);
    ((DrawAmmo_t)0x5893B0)(ped, myX, myY, alpha);
}

// === Custom Health Bar ===
void __cdecl RenderHealthBar_Custom(int playerId, int /*x*/, int /*y*/) {
    // Check visibility
    if (!g_HudLayout.showHealthBar)
        return;
    
    // Flash logic (same as original)
    if (CHud::m_ItemToFlash == 4 && (CTimer::m_FrameCounter & 8))
        return;
    
    CPlayerPed* player = FindPlayerPed(playerId);
    if (!player) return;
    
    float health = player->m_fHealth;
    if (health < 10.0f && (CTimer::m_FrameCounter & 8))
        return;
    
    CPlayerInfo& info = CWorld::Players[playerId];
    float maxHealth = static_cast<float>(info.m_nMaxHealth);
    if (maxHealth <= 0) maxHealth = 100.0f;
    
    // Bar dimensions from layout
    float barWidth = ScreenStretchX(g_HudLayout.barWidth);
    float barHeight = ScreenStretchY(g_HudLayout.barHeight);
    
    // Position: right-aligned, at configured Y position
    float barX = GetBarRightAlignedX(barWidth);
    float barY = ScreenStretchY(g_HudLayout.healthBarY);
    
    float healthPercent = (health / maxHealth) * 100.0f;
    
    // Draw with scaled border
    float scaledBorder = ScreenStretchY(1.5f);
    DrawRoundedBarWithBorder(barX, barY, barWidth, barHeight, healthPercent, 
                            HEALTH_FG, HEALTH_BG, scaledBorder, BORDER_COLOR);
}

// === Custom Armor Bar ===
void __cdecl RenderArmorBar_Custom(int playerId, int /*x*/, int /*y*/) {
    // Check visibility
    if (!g_HudLayout.showArmorBar)
        return;
    
    CPlayerPed* player = FindPlayerPed(playerId);
    if (!player) return;

    // Check flash and armor > 1 (same as original)
    if ((CHud::m_ItemToFlash == 3 && (CTimer::m_FrameCounter & 8)) || player->m_fArmour <= 1.0f)
        return;

    CPlayerInfo& info = CWorld::Players[playerId];
    float maxArmor = static_cast<float>(info.m_nMaxArmour);
    if (maxArmor <= 0) maxArmor = 100.0f;

    // Bar dimensions from layout (same width as health bar)
    float barWidth = ScreenStretchX(g_HudLayout.barWidth);
    float barHeight = ScreenStretchY(g_HudLayout.barHeight);
    
    // Position: right-aligned, at configured Y position
    float barX = GetBarRightAlignedX(barWidth);
    float barY = ScreenStretchY(g_HudLayout.armorBarY);

    float armorPercent = (player->m_fArmour / maxArmor) * 100.0f;

    // Draw with scaled border (same as health bar)
    float scaledBorder = ScreenStretchY(1.5f);
    DrawRoundedBarWithBorder(barX, barY, barWidth, barHeight, armorPercent,
        ARMOR_FG, ARMOR_BG, scaledBorder, BORDER_COLOR);
}

// === Custom Breath Bar ===
static CRGBA BREATH_FG(172, 142, 104, 255);   // Tan/Yellow
static CRGBA BREATH_BG(86, 71, 52, 180);      // Dark tan

void __cdecl RenderBreathBar_Custom(int playerId, int /*x*/, int /*y*/) {
    if (!g_HudLayout.showBreathBar)
        return;
    
    // Flash logic
    if (CHud::m_ItemToFlash == 5 && (CTimer::m_FrameCounter & 8))
        return;
    
    CPlayerPed* player = FindPlayerPed(playerId);
    if (!player || !player->m_pPlayerData) return;
    
    float breath = player->m_pPlayerData->m_fBreath;
    float maxBreath = CStats::GetFatAndMuscleModifier(STAT_MOD_AIR_IN_LUNG);
    if (maxBreath <= 0) maxBreath = 100.0f;
    
    // Bar dimensions from layout
    float barWidth = ScreenStretchX(g_HudLayout.barWidth);
    float barHeight = ScreenStretchY(g_HudLayout.barHeight);
    
    // Position: right-aligned, at configured Y position
    float barX = GetBarRightAlignedX(barWidth);
    float barY = ScreenStretchY(g_HudLayout.breathBarY);
    
    float breathPercent = (breath / maxBreath) * 100.0f;
    
    // Draw with scaled border
    float scaledBorder = ScreenStretchY(1.5f);
    DrawRoundedBarWithBorder(barX, barY, barWidth, barHeight, breathPercent,
        BREATH_FG, BREATH_BG, scaledBorder, BORDER_COLOR);
}


// === Main Plugin ===
class RoundedHUD {
public:
    RoundedHUD() {
        // =====================================================
        // REDIRECT INDIVIDUAL DRAW CALLS INSIDE DrawPlayerInfo
        // The original function still runs (state machines work!)
        // We just intercept WHERE things are drawn
        // =====================================================
        
        // Clock: Redirect CFont::PrintString call at 0x58EC21
        patch::RedirectCall(0x58EC21, ClockPrintString_Hook);
        
        // Money: Redirect CFont::PrintString call at 0x58F607
        patch::RedirectCall(0x58F607, MoneyPrintString_Hook);
        
        // Weapon Icon: Redirect CHud::DrawWeaponIcon calls
        patch::RedirectCall(0x58F944, WeaponIcon_Hook);   // Player 1
        patch::RedirectCall(0x58F9B1, WeaponIcon_Hook);   // Player 2
        
        // Ammo: Redirect CHud::DrawAmmo calls
        patch::RedirectCall(0x58FA25, Ammo_Hook);         // Player 1
        patch::RedirectCall(0x58FABE, Ammo_Hook);         // Player 2
        
        // Bar render functions (these are separate functions, still use RedirectJump)
        patch::RedirectJump(0x589270, RenderHealthBar_Custom);   // RenderHealthBar
        patch::RedirectJump(0x5890A0, RenderArmorBar_Custom);    // RenderArmorBar
        patch::RedirectJump(0x589190, RenderBreathBar_Custom);   // RenderBreathBar
        
        // =====================================================
        // HUD LAYOUT CONFIGURATION
        // =====================================================
        // All positions are in 640x448 reference coordinates
        // Adjust these to match your desired layout:
        
        g_HudLayout.rightMargin = 32.0f;   // Distance from right edge
        
        // Clock & Money
        g_HudLayout.clockY = 22.0f;
        g_HudLayout.moneyY = 43.0f;
        
        // Weapon Icon
        g_HudLayout.weaponX = 82.0f;       // From right edge
        g_HudLayout.weaponY = 22.0f;
        g_HudLayout.weaponWidth = 47.0f;
        g_HudLayout.weaponHeight = 58.0f;
        
        // Ammo (below weapon)
        g_HudLayout.ammoY = 45.0f;
        
        // Health/Armor/Breath Bars
        g_HudLayout.barWidth = 64.0f;
        g_HudLayout.barHeight = 8.0f;
        g_HudLayout.healthBarY = 64.0f;
        g_HudLayout.armorBarY = 76.0f;
        g_HudLayout.breathBarY = 88.0f;
    }
} roundedHUD;

// ============================================================================
// EXPORTED HUD LAYOUT FUNCTIONS
// These can be called from other modules/scripts at runtime
// ============================================================================

// Set all positions at once
extern "C" __declspec(dllexport) void ConfigureHudLayout(
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

// Set clock and money positions
extern "C" __declspec(dllexport) void SetClockMoneyPos(float clockY, float moneyY) {
    g_HudLayout.clockY = clockY;
    g_HudLayout.moneyY = moneyY;
}

// Set weapon icon position
extern "C" __declspec(dllexport) void SetWeaponPos(float x, float y, float width, float height) {
    g_HudLayout.weaponX = x;
    g_HudLayout.weaponY = y;
    g_HudLayout.weaponWidth = width;
    g_HudLayout.weaponHeight = height;
}

// Set bar Y positions
extern "C" __declspec(dllexport) void SetBarYPositions(float healthY, float armorY, float breathY) {
    g_HudLayout.healthBarY = healthY;
    g_HudLayout.armorBarY = armorY;
    g_HudLayout.breathBarY = breathY;
}

// Set bar dimensions
extern "C" __declspec(dllexport) void SetBarDimensions(float width, float height) {
    g_HudLayout.barWidth = width;
    g_HudLayout.barHeight = height;
}

// Set right margin
extern "C" __declspec(dllexport) void SetRightMargin(float margin) {
    g_HudLayout.rightMargin = margin;
}

// Set element visibility
extern "C" __declspec(dllexport) void SetHudElementsVisible(
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