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
#include <CWanted.h>
#include <CRadar.h>
#include <CEntryExitManager.h>
#include <CMenuManager.h>
#include <CSprite2d.h>
#include <CStreaming.h>
#include <CTheScripts.h>
#include <CTxdStore.h>

#include <algorithm>
#include <cmath>

using namespace plugin;

CHudLayout1991 HudLayout;

namespace CHud1991
{

    // ============================================================================
    // POSITION HELPERS - All return REFERENCE coordinates (640x448 space)
    // Callers must use Screen::StretchY() to convert to screen coordinates
    // ============================================================================

    // Get clock/money text height in reference coords
    static float GetTextHeight()
    {
        // Font scale from game memory, multiply by base height, convert screen->reference
        return Screen::GetClockFontScaleY() * 18.0f;
    }

    static float GetClockY()
    {
        return HudLayout.hudStartY;
    }

    static float GetMoneyY()
    {
        return GetClockY() + GetTextHeight() * HudLayout.textLineHeight + HudLayout.elementSpacing;
    }

    static float GetWeaponY()
    {
        return HudLayout.hudStartY;
    }

    static float GetAmmoY()
    {
        // Uses same weaponHeight that's patched into the game's weapon drawing
        return GetWeaponY() + HudLayout.weaponHeight * HudLayout.ammoOffsetY;
    }

    static float GetBarsStartY()
    {
        return GetMoneyY() + GetTextHeight() + HudLayout.elementSpacing;
    }

    // Returns Y position for bar at given slot (0 = first, 1 = second, etc.)
    static float GetBarSlotY(int slot)
    {
        float totalHeight = HudLayout.barHeight + HudLayout.barBorderWidth * 2;
        return GetBarsStartY() + slot * (totalHeight + HudLayout.barSpacing);
    }

    static void DrawRoundedBar(float x, float y, float w, float h, float percent,
                               CRGBA fg, CRGBA bg)
    {
        constexpr float pi = 3.14159265358979323846f;
        float r = h / 2.0f;
        float body = w - h;

        percent = std::clamp(percent, 0.0f, 100.0f);

        Drawing::SetupRenderState();

        // Background pill
        Drawing::Semicircle(x + r, y + r, r, pi * 0.5f, pi * 1.5f, HudLayout.barSegments, bg);
        if (body > 0)
            Drawing::FilledRect(x + r, y, body, h, bg);
        Drawing::Semicircle(x + w - r, y + r, r, -pi * 0.5f, pi * 0.5f, HudLayout.barSegments, bg);

        // Foreground fill
        if (percent > 0)
        {
            float fill = w * (percent / 100.0f);

            // Left cap
            if (fill >= r * 0.99f)
            {
                Drawing::Semicircle(x + r, y + r, r, pi * 0.5f, pi * 1.5f, HudLayout.barSegments, fg);
            }
            else if (fill > 0)
            {
                Drawing::PartialLeftCapFill(x + r, y + r, r, fill, HudLayout.barCapSlices, fg);
            }

            // Body
            if (fill > r && body > 0)
            {
                float bodyFill = std::min(fill - r, body);
                Drawing::FilledRect(x + r, y, bodyFill, h, fg);
            }

            // Right cap
            if (fill > w - r)
            {
                Drawing::PartialRightCapFill(x + w - r, y + r, r, fill - (w - r), HudLayout.barCapSlices, fg);
            }
        }
    }

    static void DrawRoundedBarWithBorder(float x, float y, float w, float h, float percent,
                                         CRGBA fg, CRGBA bg, float border, CRGBA borderCol)
    {

        DrawRoundedBar(x - border, y - border, w + border * 2, h + border * 2, 100.0f, borderCol, borderCol);
        DrawRoundedBar(x, y, w, h, percent, fg, bg);
    }

    struct RadarViewportGeometry
    {
        float left;
        float top;
        float right;
        float bottom;
    };

    static bool IsFrontendMapDrawing()
    {
        return *reinterpret_cast<bool *>(0xBA67A1);
    }

    static RadarViewportGeometry GetRadarViewportGeometry()
    {
        RadarViewportGeometry geometry{};
        float width = Screen::StretchY(HudLayout.radarViewportWidth);
        float height = Screen::StretchY(HudLayout.radarViewportHeight);

        // Use a dedicated visual margin: the weapon icon's anchor and sprite
        // rectangle do not match its visible texture bounds.
        geometry.left = Screen::StretchY(
            HudLayout.radarLeftMargin +
            HudLayout.radarHousingPadding +
            HudLayout.radarHousingBorder
        );
        geometry.bottom = static_cast<float>(RsGlobal.maximumHeight) -
                          Screen::StretchY(HudLayout.radarViewportBottomMargin);
        geometry.right = geometry.left + width;
        geometry.top = geometry.bottom - height;
        return geometry;
    }

    static float GetRadarHorizontalScale()
    {
        RadarViewportGeometry geometry = GetRadarViewportGeometry();
        float width = geometry.right - geometry.left;
        float height = geometry.bottom - geometry.top;
        return width > 0.001f ? height / width : 1.0f;
    }

    static float __cdecl LimitRadarPointToRectangle(CVector2D &point)
    {
        float magnitude = std::sqrt(point.x * point.x + point.y * point.y);

        // The frontend map uses the same function but must remain unlimited.
        if (IsFrontendMapDrawing())
            return magnitude;

        // Radar-space [-1, 1] maps directly to the viewport rectangle. Scale
        // along the point's direction until its largest axis reaches an edge.
        float extent = std::max(std::abs(point.x), std::abs(point.y));
        if (extent > 1.0f)
        {
            point.x /= extent;
            point.y /= extent;
        }

        return magnitude;
    }

    static void __cdecl TransformRadarPointToScreenSpaceWideImpl(CVector2D *out, const CVector2D *in)
    {
        if (!out || !in)
            return;

        if (IsFrontendMapDrawing())
        {
            float zoom = *reinterpret_cast<float *>(0xBA67AC);
            const CVector2D &origin = *reinterpret_cast<CVector2D *>(0xBA67B0);
            out->x = origin.x + zoom * in->x;
            out->y = origin.y - zoom * in->y;
            return;
        }

        RadarViewportGeometry geometry = GetRadarViewportGeometry();
        float halfWidth = (geometry.right - geometry.left) * 0.5f;
        float halfHeight = (geometry.bottom - geometry.top) * 0.5f;
        out->x = geometry.left + halfWidth + halfWidth * in->x;
        out->y = geometry.top + halfHeight - halfHeight * in->y;
    }

    // The stock DrawRadarSection path expects EDX to survive this function.
    static void __declspec(naked) TransformRadarPointToScreenSpaceWide()
    {
        __asm
        {
            push edx
            push dword ptr [esp + 0Ch]
            push dword ptr [esp + 0Ch]
            call TransformRadarPointToScreenSpaceWideImpl
            add esp, 8
            pop edx
            retn
        }
    }

    static void __cdecl TransformRealWorldPointToRadarSpaceWideImpl(CVector2D *out, const CVector2D *in)
    {
        if (!out || !in)
            return;

        float range = CRadar::m_radarRange;
        if (std::abs(range) < 0.001f)
            range = 1.0f;

        float x = (in->x - CRadar::vec2DRadarOrigin.x) / range;
        float y = (in->y - CRadar::vec2DRadarOrigin.y) / range;
        out->x = CRadar::cachedCos * x + CRadar::cachedSin * y;
        out->y = -CRadar::cachedSin * x + CRadar::cachedCos * y;

        if (!IsFrontendMapDrawing())
            out->x *= GetRadarHorizontalScale();
    }

    static void __declspec(naked) TransformRealWorldPointToRadarSpaceWide()
    {
        __asm
        {
            push edx
            push dword ptr [esp + 0Ch]
            push dword ptr [esp + 0Ch]
            call TransformRealWorldPointToRadarSpaceWideImpl
            add esp, 8
            pop edx
            retn
        }
    }

    static void __cdecl SetupWideRadarRect(int x, int y)
    {
        CRadar::m_radarRect.left = static_cast<float>(500 * (x - 8));
        CRadar::m_radarRect.top = static_cast<float>(500 * (8 - y));
        CRadar::m_radarRect.right = static_cast<float>(500 * (x - 3));
        CRadar::m_radarRect.bottom = static_cast<float>(500 * (3 - y));
    }

    static void __cdecl StreamWideRadarSections(int x, int y)
    {
        constexpr int radarTileCount = 12;
        constexpr int txdModelOffset = 20000;
        constexpr int tileRadius = 2;
        constexpr int streamingFlags = GAME_REQUIRED | KEEP_IN_MEMORY;

        for (int tileY = 0; tileY < radarTileCount; ++tileY)
        {
            for (int tileX = 0; tileX < radarTileCount; ++tileX)
            {
                int txdIndex = gRadarTxdIds[tileY * radarTileCount + tileX];
                if (txdIndex == -1)
                    continue;

                int modelId = txdIndex + txdModelOffset;
                if (std::abs(tileX - x) <= tileRadius && std::abs(tileY - y) <= tileRadius)
                    CStreaming::RequestModel(modelId, streamingFlags);
                else
                    CStreaming::RemoveModel(modelId);
            }
        }
    }

    static CVector2D RotateRadarPointCounterclockwise(const CVector2D &point)
    {
        CVector2D result;
        result.x = CRadar::cachedCos * point.x - CRadar::cachedSin * point.y;
        result.y = CRadar::cachedSin * point.x + CRadar::cachedCos * point.y;
        return result;
    }

    static void __cdecl DrawRadarSectionWide(int x, int y)
    {
        constexpr int radarTileCount = 12;
        bool isInBounds = x >= 0 && x < radarTileCount && y >= 0 && y < radarTileCount;

        RwRaster *raster = nullptr;
        CRGBA color(255, 255, 255, 255);
        if (!isInBounds)
        {
            color = CRGBA(111, 137, 170, 255);
        }
        else if (CTheScripts::bPlayerIsOffTheMap)
        {
            color = CRGBA(204, 204, 204, 255);
        }
        else
        {
            int txdIndex = gRadarTxdIds[y * radarTileCount + x];
            if (txdIndex == -1 || !CTxdStore::ms_pTxdPool)
                return;

            TxdDef *txd = CTxdStore::ms_pTxdPool->GetAt(txdIndex);
            if (!txd || !txd->m_pRwDictionary)
                return;

            RwTexture *texture = GetFirstTexture(txd->m_pRwDictionary);
            if (!texture)
                return;

            raster = RwTextureGetRaster(texture);
        }

        CVector2D corners[4]{};
        GetTextureCorners(x, y, corners);

        float horizontalScale = GetRadarHorizontalScale();
        CVector2D rotated[4]{};
        for (int i = 0; i < 4; ++i)
        {
            float radarX = (corners[i].x - CRadar::vec2DRadarOrigin.x) / CRadar::m_radarRange;
            float radarY = (corners[i].y - CRadar::vec2DRadarOrigin.y) / CRadar::m_radarRange;
            rotated[i].x = (CRadar::cachedCos * radarX + CRadar::cachedSin * radarY) * horizontalScale;
            rotated[i].y = -CRadar::cachedSin * radarX + CRadar::cachedCos * radarY;
        }

        CVector2D clipped[8]{};
        int vertexCount = CRadar::ClipRadarPoly(clipped, rotated);
        if (vertexCount <= 2)
            return;

        CVector2D textureCoordinates[8]{};
        CVector2D screenVertices[8]{};
        for (int i = 0; i < vertexCount; ++i)
        {
            CVector2D unscaled = clipped[i];
            if (std::abs(horizontalScale) > 0.001f)
                unscaled.x /= horizontalScale;

            CVector2D worldOffset = RotateRadarPointCounterclockwise(unscaled);
            CVector2D worldPoint;
            worldPoint.x = CRadar::vec2DRadarOrigin.x + worldOffset.x * CRadar::m_radarRange;
            worldPoint.y = CRadar::vec2DRadarOrigin.y + worldOffset.y * CRadar::m_radarRange;

            CRadar::TransformRealWorldToTexCoordSpace(textureCoordinates[i], worldPoint, x, y);
            TransformRadarPointToScreenSpaceWideImpl(&screenVertices[i], &clipped[i]);
        }

        RwRenderStateSet(rwRENDERSTATETEXTURERASTER, raster);
        CSprite2d::SetVertices(
            vertexCount,
            &screenVertices[0].x,
            &textureCoordinates[0].x,
            color
        );
        RwIm2DRenderPrimitive(rwPRIMTYPETRIFAN, CSprite2d::maVertices, vertexCount);
    }

    static void __cdecl DrawWideRadarSectionSetStart(int firstX, int firstY)
    {
        int centerX = firstX + 1;
        int centerY = firstY + 1;

        // The stock function draws the inner 3x3. Add the outer ring first,
        // then draw the first stock tile that this call replaces.
        for (int offsetY = -2; offsetY <= 2; ++offsetY)
        {
            for (int offsetX = -2; offsetX <= 2; ++offsetX)
            {
                if (std::abs(offsetX) <= 1 && std::abs(offsetY) <= 1)
                    continue;
                DrawRadarSectionWide(centerX + offsetX, centerY + offsetY);
            }
        }
        DrawRadarSectionWide(firstX, firstY);
    }

    static void __cdecl DrawNorthAtRadarEdge(unsigned short spriteId, float x, float y, unsigned char alpha)
    {
        if (!IsFrontendMapDrawing())
        {
            RadarViewportGeometry geometry = GetRadarViewportGeometry();
            float centerX = (geometry.left + geometry.right) * 0.5f;
            float centerY = (geometry.top + geometry.bottom) * 0.5f;
            float directionX = x - centerX;
            float directionY = y - centerY;

            // The north texture contains substantial transparent padding. Use
            // a visual inset instead of the full 8-unit sprite quad extent.
            float markerInset = Screen::StretchY(HudLayout.radarNorthMarkerInset);
            float halfWidth = (geometry.right - geometry.left) * 0.5f - markerInset;
            float halfHeight = (geometry.bottom - geometry.top) * 0.5f - markerInset;

            if (halfWidth > 0.0f && halfHeight > 0.0f &&
                (std::abs(directionX) > 0.001f || std::abs(directionY) > 0.001f))
            {
                float edgeFactor = std::max(
                    std::abs(directionX) / halfWidth,
                    std::abs(directionY) / halfHeight
                );
                if (edgeFactor > 0.001f)
                {
                    x = centerX + directionX / edgeFactor;
                    y = centerY + directionY / edgeFactor;
                }
            }
        }

        CRadar::DrawRadarSprite(spriteId, x, y, alpha);
    }

    static CRGBA ModulateHudColor(CRGBA color, float brightness, float alpha)
    {
        color.r = static_cast<unsigned char>(std::clamp(color.r * brightness, 0.0f, 255.0f));
        color.g = static_cast<unsigned char>(std::clamp(color.g * brightness, 0.0f, 255.0f));
        color.b = static_cast<unsigned char>(std::clamp(color.b * brightness, 0.0f, 255.0f));
        color.a = static_cast<unsigned char>(std::clamp(color.a * alpha / 255.0f, 0.0f, 255.0f));
        return color;
    }

    static void DrawWantedSirenLight(float x, float y, float w, float h, CRGBA color,
                                     bool isLit, float brightness, float alpha)
    {
        float border = Screen::StretchY(HudLayout.wantedSirenLightBorder);
        float visibility = std::clamp(alpha / 255.0f, 0.0f, 1.0f);

        if (isLit && visibility > 0.0f)
        {
            float glow = Screen::StretchY(HudLayout.wantedSirenGlowSize);
            CRGBA glowColor = ModulateHudColor(color, brightness, alpha * 0.22f);
            DrawRoundedBar(x - glow, y - glow, w + glow * 2.0f, h + glow * 2.0f,
                           100.0f, glowColor, glowColor);
        }

        float bodyBrightness = isLit
            ? 0.30f + (brightness - 0.30f) * visibility
            : 0.10f;
        CRGBA bodyColor = ModulateHudColor(color, bodyBrightness, 230.0f);
        CRGBA edgeColor = HudLayout.wantedSirenLightEdge;

        DrawRoundedBarWithBorder(x, y, w, h, 100.0f, bodyColor, bodyColor, border, edgeColor);

        if (isLit && visibility > 0.0f)
        {
            float inset = h * 0.22f;
            float highlightHeight = std::max(h * 0.16f, 1.0f);
            CRGBA highlight = ModulateHudColor(CRGBA(255, 255, 255, 105), brightness, alpha);
            Drawing::FilledRect(x + inset, y + inset, w - inset * 2.0f, highlightHeight, highlight);
        }
    }

    struct RadarUnitGeometry
    {
        float mapLeft;
        float mapTop;
        float mapRight;
        float mapBottom;
        float outerLeft;
        float outerTop;
        float outerRight;
        float outerBottom;
    };

    static bool GetRadarUnitGeometry(RadarUnitGeometry &geometry)
    {
        RadarViewportGeometry viewport = GetRadarViewportGeometry();
        geometry.mapLeft = viewport.left;
        geometry.mapTop = viewport.top;
        geometry.mapRight = viewport.right;
        geometry.mapBottom = viewport.bottom;

        if (geometry.mapRight - geometry.mapLeft <= 1.0f ||
            geometry.mapBottom - geometry.mapTop <= 1.0f)
            return false;

        float padding = Screen::StretchY(HudLayout.radarHousingPadding);
        geometry.outerLeft = geometry.mapLeft - padding;
        geometry.outerTop = geometry.mapTop - Screen::StretchY(
            HudLayout.radarSirenBezelHeight + HudLayout.radarSirenMapGap
        );
        geometry.outerRight = geometry.mapRight + padding;
        geometry.outerBottom = geometry.mapBottom + padding;
        return true;
    }

    static void DrawRadarHousingBack(const RadarUnitGeometry &geometry)
    {
        Drawing::SetupRenderState();

        float width = geometry.outerRight - geometry.outerLeft;
        float height = geometry.outerBottom - geometry.outerTop;
        float radius = Screen::StretchY(HudLayout.radarHousingCornerRadius);
        float border = Screen::StretchY(HudLayout.radarHousingBorder);
        Drawing::FilledRoundedRectWithBorder(
            geometry.outerLeft,
            geometry.outerTop,
            width,
            height,
            radius,
            border,
            HudLayout.barSegments,
            HudLayout.radarHousing,
            HudLayout.radarHousingEdge
        );

        Drawing::FilledRect(
            geometry.mapLeft,
            geometry.mapTop,
            geometry.mapRight - geometry.mapLeft,
            geometry.mapBottom - geometry.mapTop,
            CRGBA(0, 0, 0, 255)
        );
    }

    static float GetWantedDisplayAlpha(int level, int parole)
    {
        int state = *reinterpret_cast<int *>(0xBAA400);
        int fadeTimer = *reinterpret_cast<int *>(0xBAA408);
        bool isVisible = *reinterpret_cast<bool *>(0xBAB228);

        if (state == 0 || (level <= 0 && !isVisible && parole <= 0))
            return 0.0f;
        if (state == 1)
            return 255.0f;
        return std::clamp(fadeTimer * 0.255f, 0.0f, 255.0f);
    }

    static void DrawWantedSirenBar(const RadarUnitGeometry &geometry)
    {
        if (!HudLayout.showWantedSirens)
            return;

        float bayInset = Screen::StretchY(HudLayout.radarSirenInset);
        float bayLeft = geometry.outerLeft + bayInset;
        float bayTop = geometry.outerTop;
        float bayRight = geometry.outerRight - bayInset;
        float bayBottom = geometry.mapTop - Screen::StretchY(HudLayout.radarSirenMapGap);
        float bayWidth = bayRight - bayLeft;
        float bayHeight = bayBottom - bayTop;

        if (bayWidth <= 1.0f || bayHeight <= 1.0f)
            return;

        constexpr int lightCount = 6;
        float lightPadding = Screen::StretchY(HudLayout.wantedSirenLightPadding);
        float lightSpacing = Screen::StretchY(HudLayout.wantedSirenLightSpacing);
        float lightWidth = (bayWidth - lightPadding * 2.0f - lightSpacing * (lightCount - 1)) / lightCount;
        float lightHeight = std::min(
            Screen::StretchY(HudLayout.wantedSirenLightHeight),
            bayHeight - lightPadding * 2.0f
        );

        if (lightWidth <= 1.0f || lightHeight <= 1.0f)
            return;

        CWanted *wanted = FindPlayerWanted(-1);
        int level = wanted ? std::clamp(static_cast<int>(wanted->m_nWantedLevel), 0, lightCount) : 0;
        int parole = wanted ? std::clamp(static_cast<int>(wanted->m_nWantedLevelBeforeParole), 0, lightCount) : 0;
        float alpha = GetWantedDisplayAlpha(level, parole);

        uint32_t timeSinceChange = wanted
            ? CTimer::m_snTimeInMilliseconds - wanted->m_nLastTimeWantedLevelChanged
            : 0;
        bool wantedChangeFlashOn = !wanted || timeSinceChange > 2000 || (CTimer::m_FrameCounter & 4) != 0;
        bool paroleFlashOn = (CTimer::m_FrameCounter & 4) != 0;
        unsigned int pulseMs = std::max(HudLayout.wantedSirenPulseMs, 1u);
        bool redPulse = ((CTimer::m_snTimeInMilliseconds / pulseMs) & 1u) == 0;

        float lightY = bayTop + (bayHeight - lightHeight) * 0.5f;
        for (int i = 0; i < lightCount; i++)
        {
            bool isRed = (i & 1) == 0;
            bool isWanted = i < level;
            bool isParole = !isWanted && i < parole && paroleFlashOn;
            bool isLit = isWanted || isParole;

            float brightness = (isRed == redPulse) ? 1.0f : 0.72f;
            if (isWanted && !wantedChangeFlashOn)
                brightness *= 0.42f;
            if (isParole)
                brightness *= 0.65f;

            float lightX = bayLeft + lightPadding + i * (lightWidth + lightSpacing);
            DrawWantedSirenLight(
                lightX,
                lightY,
                lightWidth,
                lightHeight,
                isRed ? HudLayout.wantedSirenRed : HudLayout.wantedSirenBlue,
                isLit,
                brightness,
                alpha
            );
        }
    }

    static void DrawRadarHousingFront(const RadarUnitGeometry &geometry)
    {
        Drawing::SetupRenderState();

        DrawWantedSirenBar(geometry);
    }

    static bool ShouldDrawUnifiedRadar()
    {
        if (CEntryExitManager::ms_exitEnterState == 1 ||
            CEntryExitManager::ms_exitEnterState == 2 ||
            FrontEndMenuManager.m_nPrefsRadarMode != 0)
            return false;

        if (CHud::m_ItemToFlash == ITEM_RADAR && (CTimer::m_FrameCounter & 8) == 0)
            return false;

        return true;
    }

    static void __cdecl DrawUnifiedRadar()
    {
        RadarUnitGeometry geometry{};
        bool drawHousing = ShouldDrawUnifiedRadar() && GetRadarUnitGeometry(geometry);

        if (drawHousing)
            DrawRadarHousingBack(geometry);

        reinterpret_cast<void(__cdecl *)()>(0x58A330)();

        if (drawHousing)
            DrawRadarHousingFront(geometry);
    }

    static void __cdecl UseRectangularRadarMask()
    {
        // Radar polygons are already clipped to [-1, 1]. Skipping the stock
        // circular corner mask lets those polygons fill the complete viewport.
    }

    static void __declspec(naked) SkipStockRadarDisc()
    {
        __asm retn 8
    }

    static void __cdecl DrawClock(float, float, char *text)
    {
        if (!HudLayout.showClock)
            return;
        CFont::PrintString(Screen::FromRight(HudLayout.statsRightMargin), Screen::StretchY(GetClockY()), text);
    }

    static void __cdecl DrawMoney(float, float, char *text)
    {
        if (!HudLayout.showMoney)
            return;
        CFont::PrintString(Screen::FromRight(HudLayout.statsRightMargin), Screen::StretchY(GetMoneyY()), text);
    }

    static void __cdecl DrawWeaponIcon(CPed *ped, int, int, float alpha)
    {
        if (!HudLayout.showWeapon || !ped)
            return;

        int x = static_cast<int>(Screen::FromRight(HudLayout.weaponRightMargin));
        int y = static_cast<int>(Screen::StretchY(GetWeaponY()));

        reinterpret_cast<void(__cdecl *)(CPed *, int, int, float)>(0x58D7D0)(ped, x, y, alpha);
    }

    static void __cdecl DrawAmmo(CPed *ped, int, int, float alpha)
    {
        if (!HudLayout.showAmmo || !ped)
            return;

        int x = static_cast<int>(Screen::FromRight(HudLayout.weaponRightMargin - HudLayout.weaponWidth / 2.0f));
        int y = static_cast<int>(Screen::StretchY(GetAmmoY()));

        reinterpret_cast<void(__cdecl *)(CPed *, int, int, float)>(0x5893B0)(ped, x, y, alpha);
    }

    static void __cdecl DrawHealthBar(int playerId, int, int)
    {
        if (!HudLayout.showHealthBar)
            return;
        if (CHud::m_ItemToFlash == 4 && (CTimer::m_FrameCounter & 8))
            return;

        CPlayerPed *player = FindPlayerPed(playerId);
        if (!player)
            return;

        float health = player->m_fHealth;
        if (health < 10.0f && (CTimer::m_FrameCounter & 8))
            return;

        float maxHealth = static_cast<float>(CWorld::Players[playerId].m_nMaxHealth);
        if (maxHealth <= 0)
            maxHealth = 100.0f;

        float w = Screen::StretchY(HudLayout.barWidth);
        float h = Screen::StretchY(HudLayout.barHeight);
        float x = Screen::GetBarX(w);
        float y = Screen::StretchY(GetBarSlotY(0)); // Always slot 0
        float border = Screen::StretchY(HudLayout.barBorderWidth);

        DrawRoundedBarWithBorder(x, y, w, h, (health / maxHealth) * 100.0f,
                                 HudLayout.healthFG, HudLayout.healthBG, border, HudLayout.barBorderColor);
    }

    static void __cdecl DrawArmorBar(int playerId, int, int)
    {
        if (!HudLayout.showArmorBar)
            return;

        CPlayerPed *player = FindPlayerPed(playerId);
        if (!player)
            return;
        if ((CHud::m_ItemToFlash == 3 && (CTimer::m_FrameCounter & 8)) || player->m_fArmour <= 1.0f)
            return;

        float maxArmor = static_cast<float>(CWorld::Players[playerId].m_nMaxArmour);
        if (maxArmor <= 0)
            maxArmor = 100.0f;

        float w = Screen::StretchY(HudLayout.barWidth);
        float h = Screen::StretchY(HudLayout.barHeight);
        float x = Screen::GetBarX(w);
        float y = Screen::StretchY(GetBarSlotY(1)); // Slot 1 (after health)
        float border = Screen::StretchY(HudLayout.barBorderWidth);

        DrawRoundedBarWithBorder(x, y, w, h, (player->m_fArmour / maxArmor) * 100.0f,
                                 HudLayout.armorFG, HudLayout.armorBG, border, HudLayout.barBorderColor);
    }

    static void __cdecl DrawBreathBar(int playerId, int, int)
    {
        if (!HudLayout.showBreathBar)
            return;
        if (CHud::m_ItemToFlash == 5 && (CTimer::m_FrameCounter & 8))
            return;

        CPlayerPed *player = FindPlayerPed(playerId);
        if (!player || !player->m_pPlayerData)
            return;

        float breath = player->m_pPlayerData->m_fBreath;
        float maxBreath = CStats::GetFatAndMuscleModifier(STAT_MOD_AIR_IN_LUNG);
        if (maxBreath <= 0)
            maxBreath = 100.0f;

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

    static void __cdecl UpdateWantedDisplay()
    {
        CWanted *wanted = FindPlayerWanted(-1);
        if (!wanted || CHud::bDrawingVitalStats)
            return;

        int level = std::clamp(static_cast<int>(wanted->m_nWantedLevel), 0, 6);

        // Fade state machine (game statics)
        static int &state = *(int *)0xBAA400;
        static int &timer = *(int *)0xBAA404;
        static int &fadeTimer = *(int *)0xBAA408;
        static int &lastLevel = *(int *)0xBAA40C;
        static bool &isVisible = *(bool *)0xBAB228;

        if (lastLevel != level)
        {
            if (state == 0)
                fadeTimer = 0;
            timer = 5;
            state = 2;
        }

        if (state)
        {
            int step = static_cast<int>(CTimer::ms_fTimeStep * 20.0f);

            switch (state)
            {
            case 1: // Visible
                fadeTimer = 1000;
                if (timer > 10000)
                {
                    state = 3;
                    fadeTimer = 3000;
                }
                timer += step;
                break;
            case 2: // Fade in
                fadeTimer += step;
                if (fadeTimer > 1000)
                {
                    fadeTimer = 1000;
                    state = 1;
                }
                timer += step;
                break;
            case 3: // Fade out
                fadeTimer -= step;
                if (fadeTimer < 0)
                {
                    fadeTimer = 0;
                    state = 0;
                }
                timer += step;
                break;
            }
            isVisible = (state == 1);
        }

        lastLevel = level;
    }

    void InstallHooks()
    {
        patch::RedirectCall(0x58EC21, DrawClock);

        static const char *fmtPos = "$%d";
        static const char *fmtNeg = "-$%d";
        patch::SetPointer(0x58F4C7 + 1, fmtPos);
        patch::SetPointer(0x58F509 + 1, fmtNeg);
        patch::RedirectCall(0x58F607, DrawMoney);

        patch::RedirectCall(0x58F944, DrawWeaponIcon);
        patch::RedirectCall(0x58FA25, DrawAmmo);

        patch::RedirectJump(0x589270, DrawHealthBar);
        patch::RedirectJump(0x5890A0, DrawArmorBar);
        patch::RedirectJump(0x589190, DrawBreathBar);

        patch::RedirectJump(0x58D9A0, UpdateWantedDisplay);

        patch::RedirectJump(0x5832F0, LimitRadarPointToRectangle);
        patch::RedirectJump(0x583480, TransformRadarPointToScreenSpaceWide);
        patch::RedirectJump(0x583530, TransformRealWorldPointToRadarSpaceWide);
        patch::RedirectJump(0x584A80, SetupWideRadarRect);
        patch::RedirectJump(0x584C50, StreamWideRadarSections);
        patch::RedirectJump(0x586110, DrawRadarSectionWide);
        patch::RedirectCall(0x586976, DrawWideRadarSectionSetStart);
        patch::RedirectCall(0x588188, DrawNorthAtRadarEdge);

        patch::RedirectCall(0x58FC53, DrawUnifiedRadar);
        patch::RedirectCall(0x586887, UseRectangularRadarMask);
        patch::RedirectCall(0x58A823, SkipStockRadarDisc);
        patch::RedirectCall(0x58A8CD, SkipStockRadarDisc);
        patch::RedirectCall(0x58A977, SkipStockRadarDisc);
        patch::RedirectCall(0x58AA25, SkipStockRadarDisc);

        patch::SetPointer(0x58D894 + 2, &HudLayout.weaponHeight); // fmul height (first)
        patch::SetPointer(0x58D8C9 + 2, &HudLayout.weaponWidth);  // fmul width (first)
        patch::SetPointer(0x58D933 + 2, &HudLayout.weaponWidth);  // fmul width (second)
        patch::SetPointer(0x58D94B + 2, &HudLayout.weaponHeight); // fmul height (second)
    }

}
