#pragma once

// ============================================================================
// DRAWING PRIMITIVES
// RenderWare immediate mode 2D rendering
// ============================================================================

#define DRAW_MAX_SEGMENTS 32

#include <CSprite2d.h>
#include <RenderWare.h>
#include <cmath>

namespace Drawing {

// ============================================================================
// RENDER STATE
// ============================================================================

// Call once before multiple draw calls. RenderWare render state persists until
// changed, so there's no need to set it before each primitive.
inline void SetupRenderState() {
    RwRenderStateSet(rwRENDERSTATETEXTURERASTER, nullptr);
    RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
    RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
    RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
}

// ============================================================================
// VERTEX HELPERS
// ============================================================================

inline void SetVertex(RwIm2DVertex& vtx, float x, float y, CRGBA color) {
    RwIm2DVertexSetScreenX(&vtx, x);
    RwIm2DVertexSetScreenY(&vtx, y);
    RwIm2DVertexSetScreenZ(&vtx, CSprite2d::NearScreenZ);
    RwIm2DVertexSetRecipCameraZ(&vtx, CSprite2d::RecipNearClip);
    RwIm2DVertexSetU(&vtx, 0.0f, CSprite2d::RecipNearClip);
    RwIm2DVertexSetV(&vtx, 0.0f, CSprite2d::RecipNearClip);
    RwIm2DVertexSetIntRGBA(&vtx, color.r, color.g, color.b, color.a);
}

// ============================================================================
// SHAPES
// ============================================================================

inline void FilledRect(float x, float y, float w, float h, CRGBA color) {
    RwIm2DVertex verts[4];
    SetVertex(verts[0], x, y + h, color);
    SetVertex(verts[1], x + w, y + h, color);
    SetVertex(verts[2], x + w, y, color);
    SetVertex(verts[3], x, y, color);
    RwIm2DRenderPrimitive(rwPRIMTYPETRIFAN, verts, 4);
}

inline void Semicircle(float cx, float cy, float radius, float startAngle, float endAngle,
                       int segments, CRGBA color) {
    if (segments > DRAW_MAX_SEGMENTS) segments = DRAW_MAX_SEGMENTS;
    RwIm2DVertex verts[DRAW_MAX_SEGMENTS + 2];

    SetVertex(verts[0], cx, cy, color);
    float angleStep = (endAngle - startAngle) / segments;

    for (int i = 0; i <= segments; i++) {
        float angle = startAngle + i * angleStep;
        SetVertex(verts[i + 1], cx + cosf(angle) * radius, cy + sinf(angle) * radius, color);
    }

    RwIm2DRenderPrimitive(rwPRIMTYPETRIFAN, verts, segments + 2);
}

// ============================================================================
// PARTIAL CAP FILLS (for rounded bar animation)
// Optimized: single draw call using triangle strip instead of per-slice calls
// ============================================================================

inline void PartialLeftCapFill(float cx, float cy, float radius, float fillWidth,
                                int slices, CRGBA color) {
    if (fillWidth <= 0 || slices < 1) return;
    if (fillWidth > radius) fillWidth = radius;
    if (slices > DRAW_MAX_SEGMENTS) slices = DRAW_MAX_SEGMENTS;

    // 2 vertices per boundary (top + bottom), slices+1 boundaries
    RwIm2DVertex verts[(DRAW_MAX_SEGMENTS + 1) * 2];

    float sliceW = fillWidth / slices;
    float leftEdge = cx - radius;
    float r2 = radius * radius;
    int vertCount = 0;

    for (int i = 0; i <= slices; i++) {
        float x = leftEdge + i * sliceW;
        float dx = cx - x;
        float hSq = r2 - dx * dx;
        float h = (hSq > 0) ? sqrtf(hSq) : 0.0f;

        SetVertex(verts[vertCount++], x, cy - h, color);
        SetVertex(verts[vertCount++], x, cy + h, color);
    }

    if (vertCount >= 4) {
        RwIm2DRenderPrimitive(rwPRIMTYPETRISTRIP, verts, vertCount);
    }
}

inline void PartialRightCapFill(float cx, float cy, float radius, float fillWidth,
                                 int slices, CRGBA color) {
    if (fillWidth <= 0 || slices < 1) return;
    if (fillWidth > radius) fillWidth = radius;
    if (slices > DRAW_MAX_SEGMENTS) slices = DRAW_MAX_SEGMENTS;

    RwIm2DVertex verts[(DRAW_MAX_SEGMENTS + 1) * 2];

    float sliceW = fillWidth / slices;
    float r2 = radius * radius;
    int vertCount = 0;

    for (int i = 0; i <= slices; i++) {
        float x = cx + i * sliceW;
        float dx = i * sliceW;
        if (dx > radius) dx = radius;
        float hSq = r2 - dx * dx;
        float h = (hSq > 0) ? sqrtf(hSq) : 0.0f;

        SetVertex(verts[vertCount++], x, cy - h, color);
        SetVertex(verts[vertCount++], x, cy + h, color);
    }

    if (vertCount >= 4) {
        RwIm2DRenderPrimitive(rwPRIMTYPETRISTRIP, verts, vertCount);
    }
}

} // namespace Drawing
