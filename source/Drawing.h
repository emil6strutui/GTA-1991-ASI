#pragma once

#include <CSprite2d.h>
#include <RenderWare.h>
#include <cmath>

// ============================================================================
// DRAWING PRIMITIVES
// Low-level 2D rendering helpers using RenderWare immediate mode
// ============================================================================

namespace Drawing {

// Setup render state for 2D drawing
inline void SetupRenderState() {
    RwRenderStateSet(rwRENDERSTATETEXTURERASTER, nullptr);
    RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
    RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
    RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
}

// Set vertex properties
inline void SetVertex(RwIm2DVertex& vtx, float x, float y, CRGBA color) {
    RwIm2DVertexSetScreenX(&vtx, x);
    RwIm2DVertexSetScreenY(&vtx, y);
    RwIm2DVertexSetScreenZ(&vtx, CSprite2d::NearScreenZ);
    RwIm2DVertexSetRecipCameraZ(&vtx, CSprite2d::RecipNearClip);
    RwIm2DVertexSetU(&vtx, 0.0f, CSprite2d::RecipNearClip);
    RwIm2DVertexSetV(&vtx, 0.0f, CSprite2d::RecipNearClip);
    RwIm2DVertexSetIntRGBA(&vtx, color.r, color.g, color.b, color.a);
}

// Draw a semicircle (arc with center fill)
inline void Semicircle(float cx, float cy, float radius, float startAngle, float endAngle, 
                       int segments, CRGBA color) {
    SetupRenderState();
    
    RwIm2DVertex verts[8];
    int numVerts = (segments + 2 < 8) ? (segments + 2) : 8;
    
    SetVertex(verts[0], cx, cy, color);
    
    float angleStep = (endAngle - startAngle) / segments;
    for (int i = 0; i <= segments && i < 7; i++) {
        float angle = startAngle + i * angleStep;
        float x = cx + cosf(angle) * radius;
        float y = cy + sinf(angle) * radius;
        SetVertex(verts[i + 1], x, y, color);
    }
    
    RwIm2DRenderPrimitive(rwPRIMTYPETRIFAN, verts, numVerts);
}

// Draw a filled rectangle
inline void FilledRect(float x, float y, float w, float h, CRGBA color) {
    SetupRenderState();
    
    RwIm2DVertex verts[4];
    SetVertex(verts[0], x, y + h, color);
    SetVertex(verts[1], x + w, y + h, color);
    SetVertex(verts[2], x + w, y, color);
    SetVertex(verts[3], x, y, color);
    
    RwIm2DRenderPrimitive(rwPRIMTYPETRIFAN, verts, 4);
}

// Draw partial left cap fill using vertical slices following the curve
inline void PartialLeftCapFill(float capCenterX, float capCenterY, float radius, 
                                float fillWidth, int numSlices, CRGBA color) {
    if (fillWidth <= 0 || numSlices < 1) return;
    
    SetupRenderState();
    
    if (fillWidth > radius) fillWidth = radius;
    
    float sliceWidth = fillWidth / numSlices;
    float leftEdge = capCenterX - radius;
    
    for (int i = 0; i < numSlices; i++) {
        float x1 = leftEdge + i * sliceWidth;
        float x2 = x1 + sliceWidth;
        
        // Distance from center (going left from capCenterX)
        float dx1 = capCenterX - x1;
        float dx2 = capCenterX - x2;
        
        if (dx1 < 0) dx1 = 0;
        if (dx2 < 0) dx2 = 0;
        
        float h1 = sqrtf(radius * radius - dx1 * dx1);
        float h2 = sqrtf(radius * radius - dx2 * dx2);
        
        RwIm2DVertex verts[4];
        SetVertex(verts[0], x1, capCenterY + h1, color);
        SetVertex(verts[1], x2, capCenterY + h2, color);
        SetVertex(verts[2], x2, capCenterY - h2, color);
        SetVertex(verts[3], x1, capCenterY - h1, color);
        
        RwIm2DRenderPrimitive(rwPRIMTYPETRIFAN, verts, 4);
    }
}

// Draw partial right cap fill using vertical slices following the curve
inline void PartialRightCapFill(float capCenterX, float capCenterY, float radius, 
                                 float fillWidth, int numSlices, CRGBA color) {
    if (fillWidth <= 0 || numSlices < 1) return;
    
    SetupRenderState();
    
    float sliceWidth = fillWidth / numSlices;
    
    for (int i = 0; i < numSlices; i++) {
        float x1 = capCenterX + i * sliceWidth;
        float x2 = x1 + sliceWidth;
        
        float dx1 = i * sliceWidth;
        float dx2 = (i + 1) * sliceWidth;
        
        if (dx1 > radius) dx1 = radius;
        if (dx2 > radius) dx2 = radius;
        
        float h1 = sqrtf(radius * radius - dx1 * dx1);
        float h2 = sqrtf(radius * radius - dx2 * dx2);
        
        RwIm2DVertex verts[4];
        SetVertex(verts[0], x1, capCenterY + h1, color);
        SetVertex(verts[1], x2, capCenterY + h2, color);
        SetVertex(verts[2], x2, capCenterY - h2, color);
        SetVertex(verts[3], x1, capCenterY - h1, color);
        
        RwIm2DRenderPrimitive(rwPRIMTYPETRIFAN, verts, 4);
    }
}

} // namespace Drawing

