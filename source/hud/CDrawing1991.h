#pragma once

// ============================================================================
// DRAWING PRIMITIVES
// Anti-aliased convex shapes for RenderWare immediate mode 2D rendering.
// All coordinates and sizes are in screen pixels. For plain rectangles use
// CSprite2d::DrawRect.
// ============================================================================

#include <CSprite2d.h>
#include <CVector2D.h>
#include <RenderWare.h>
#include <algorithm>
#include <cmath>
#include <numbers>

namespace Drawing {

// Rounded edges use as many chords as needed to stay within this distance of
// the true arc, so they remain round at any resolution.
constexpr float MaxArcErrorPixels = 0.1f;
constexpr int MaxArcSegments = 32;

// Width of the edge band that fades from the fill colour to transparent.
// This is the anti-aliasing; it does not depend on the game's MSAA setting.
constexpr float FringeWidthPixels = 1.0f;

// Limits how far the fringe extends at sharp corners (e.g. where a partial
// fill cuts through a rounded end), as a multiple of half the fringe width.
constexpr float MaxMiterScale = 2.0f;

// A rounded rectangle has four arcs; clipping a convex outline adds one point.
constexpr int MaxOutlinePoints = 4 * (MaxArcSegments + 1) + 1;

// ============================================================================
// RENDER STATE
// ============================================================================

// Call once before multiple draw calls. RenderWare render state persists until
// changed, so there's no need to set it before each primitive.
// CSprite2d::DrawRect turns vertex alpha off again, so call this after it.
inline void SetupRenderState() {
    RwRenderStateSet(rwRENDERSTATETEXTURERASTER, nullptr);
    RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
    RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
    RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
}

// ============================================================================
// VERTEX HELPERS
// ============================================================================

inline void SetVertex(RwIm2DVertex& vtx, const CVector2D& position, CRGBA color) {
    RwIm2DVertexSetScreenX(&vtx, position.x);
    RwIm2DVertexSetScreenY(&vtx, position.y);
    RwIm2DVertexSetScreenZ(&vtx, CSprite2d::NearScreenZ);
    RwIm2DVertexSetRecipCameraZ(&vtx, CSprite2d::RecipNearClip);
    RwIm2DVertexSetU(&vtx, 0.0f, CSprite2d::RecipNearClip);
    RwIm2DVertexSetV(&vtx, 0.0f, CSprite2d::RecipNearClip);
    RwIm2DVertexSetIntRGBA(&vtx, color.r, color.g, color.b, color.a);
}

// ============================================================================
// OUTLINES
// A closed convex polygon; the last point connects back to the first.
// ============================================================================

struct Outline {
    CVector2D points[MaxOutlinePoints];
    int count = 0;

    // Skips points that repeat the previous one, which would otherwise create
    // zero-length edges with no usable normal.
    void Add(const CVector2D& point) {
        constexpr float minSpacingSq = 1e-6f;
        if (count > 0 && (point - points[count - 1]).MagnitudeSqr() < minSpacingSq)
            return;
        if (count < MaxOutlinePoints)
            points[count++] = point;
    }
};

inline int ArcSegments(float radius, float sweep) {
    if (radius <= MaxArcErrorPixels)
        return 1;
    // A chord spanning angle a lies radius * (1 - cos(a / 2)) inside the arc.
    float step = 2.0f * std::acos(1.0f - MaxArcErrorPixels / radius);
    return std::clamp(static_cast<int>(std::ceil(sweep / step)), 1, MaxArcSegments);
}

// Angles are in radians; with screen Y pointing down, increasing angles run
// clockwise. Both end points are added.
inline void AppendArc(Outline& out, const CVector2D& center, float radius,
                      float startAngle, float endAngle) {
    int segments = ArcSegments(radius, std::abs(endAngle - startAngle));
    for (int i = 0; i <= segments; i++) {
        float angle = startAngle + (endAngle - startAngle) * i / segments;
        out.Add(center + CVector2D(std::cos(angle), std::sin(angle)) * radius);
    }
}

inline void RoundedRectOutline(Outline& out, float x, float y, float w, float h, float radius) {
    constexpr float pi = std::numbers::pi_v<float>;

    out.count = 0;
    if (w <= 0.0f || h <= 0.0f)
        return;

    radius = std::clamp(radius, 0.0f, std::min(w, h) * 0.5f);
    float left = x + radius;
    float right = x + w - radius;
    float top = y + radius;
    float bottom = y + h - radius;

    AppendArc(out, CVector2D(left, top), radius, pi, pi * 1.5f);
    AppendArc(out, CVector2D(right, top), radius, pi * 1.5f, pi * 2.0f);
    AppendArc(out, CVector2D(right, bottom), radius, 0.0f, pi * 0.5f);
    AppendArc(out, CVector2D(left, bottom), radius, pi * 0.5f, pi);
}

// A rectangle whose short sides are full semicircles.
inline void PillOutline(Outline& out, float x, float y, float w, float h) {
    RoundedRectOutline(out, x, y, w, h, std::min(w, h) * 0.5f);
}

// Keeps the part of a convex outline at or left of clipX (Sutherland-Hodgman
// against one edge). The result is convex and follows the same edges, so a
// partial fill lines up exactly with the full shape drawn behind it.
inline void ClipOutlineLeftOf(const Outline& in, float clipX, Outline& out) {
    out.count = 0;
    for (int i = 0; i < in.count; i++) {
        const CVector2D& a = in.points[i];
        const CVector2D& b = in.points[(i + 1) % in.count];
        bool aInside = a.x <= clipX;
        bool bInside = b.x <= clipX;

        if (aInside)
            out.Add(a);
        if (aInside != bInside) {
            float t = (clipX - a.x) / (b.x - a.x);
            out.Add(CVector2D(clipX, a.y + (b.y - a.y) * t));
        }
    }
}

// ============================================================================
// ANTI-ALIASED FILL
// ============================================================================

// Fills a convex outline in one draw call. Each outline point becomes an inner
// vertex (full alpha) and an outer vertex (zero alpha) half a fringe width to
// either side of the edge, so coverage ramps across the edge pixels the way
// Dear ImGui's ImDrawList::AddConvexPolyFilled anti-aliases with vertex alpha.
// Needs rwRENDERSTATEVERTEXALPHAENABLE and alpha blending (SetupRenderState);
// sets Gouraud shading itself.
inline void FillConvex(const Outline& outline, CRGBA color) {
    const CVector2D* p = outline.points;
    int n = outline.count;
    if (n > 1 && (p[n - 1] - p[0]).MagnitudeSqr() < 1e-6f)
        n--; // the closing edge would have zero length
    if (n < 3 || color.a == 0)
        return;

    // Shoelace sign gives the winding, so normals point outward either way.
    float doubleArea = 0.0f;
    for (int i = 0; i < n; i++)
        doubleArea += p[i].Cross(p[(i + 1) % n]);
    if (doubleArea == 0.0f)
        return;
    float winding = doubleArea > 0.0f ? 1.0f : -1.0f;

    CVector2D edgeNormals[MaxOutlinePoints];
    for (int i = 0; i < n; i++) {
        CVector2D edge = (p[(i + 1) % n] - p[i]).Normalized();
        edgeNormals[i] = CVector2D(edge.y, -edge.x) * winding;
    }

    RwIm2DVertex vertices[MaxOutlinePoints * 2];
    CRGBA transparent = color;
    transparent.a = 0;

    for (int i = 0; i < n; i++) {
        // Miter: the averaged normal m, divided by |m|^2, offsets both
        // adjacent edges by exactly half the fringe width.
        CVector2D miter = (edgeNormals[(i + n - 1) % n] + edgeNormals[i]) * 0.5f;
        float lengthSq = miter.MagnitudeSqr();
        if (lengthSq > 1e-6f)
            miter *= std::min(1.0f / lengthSq, MaxMiterScale * MaxMiterScale);
        miter *= FringeWidthPixels * 0.5f;

        SetVertex(vertices[i * 2], p[i] - miter, color);
        SetVertex(vertices[i * 2 + 1], p[i] + miter, transparent);
    }

    RwImVertexIndex indices[(MaxOutlinePoints - 2) * 3 + MaxOutlinePoints * 6];
    int indexCount = 0;

    // Solid interior: a fan over the inner vertices.
    for (int i = 2; i < n; i++) {
        indices[indexCount++] = 0;
        indices[indexCount++] = static_cast<RwImVertexIndex>((i - 1) * 2);
        indices[indexCount++] = static_cast<RwImVertexIndex>(i * 2);
    }

    // Fringe: one quad per edge between the inner and outer vertices.
    for (int i0 = n - 1, i1 = 0; i1 < n; i0 = i1++) {
        auto inner0 = static_cast<RwImVertexIndex>(i0 * 2);
        auto outer0 = static_cast<RwImVertexIndex>(i0 * 2 + 1);
        auto inner1 = static_cast<RwImVertexIndex>(i1 * 2);
        auto outer1 = static_cast<RwImVertexIndex>(i1 * 2 + 1);
        indices[indexCount++] = inner1;
        indices[indexCount++] = inner0;
        indices[indexCount++] = outer0;
        indices[indexCount++] = outer0;
        indices[indexCount++] = outer1;
        indices[indexCount++] = inner1;
    }

    // CHud::Draw and the radar select flat shading, under which D3D9 uses the
    // first vertex's colour and alpha for the whole triangle and the fringe
    // would not fade. Interpolate for this draw, then restore their mode.
    RwShadeMode previousShadeMode = rwSHADEMODEFLAT;
    RwRenderStateGet(rwRENDERSTATESHADEMODE, &previousShadeMode);
    RwRenderStateSet(rwRENDERSTATESHADEMODE, (void*)rwSHADEMODEGOURAUD);
    RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, vertices, n * 2, indices, indexCount);
    RwRenderStateSet(rwRENDERSTATESHADEMODE, (void*)previousShadeMode);
}

// ============================================================================
// SHAPES
// ============================================================================

inline void FilledRoundedRect(float x, float y, float w, float h, float radius, CRGBA color) {
    Outline outline;
    RoundedRectOutline(outline, x, y, w, h, radius);
    FillConvex(outline, color);
}

inline void FilledRoundedRectWithBorder(float x, float y, float w, float h, float radius,
                                        float border, CRGBA color, CRGBA borderColor) {
    FilledRoundedRect(
        x - border,
        y - border,
        w + border * 2.0f,
        h + border * 2.0f,
        radius + border,
        borderColor
    );
    FilledRoundedRect(x, y, w, h, radius, color);
}

} // namespace Drawing
