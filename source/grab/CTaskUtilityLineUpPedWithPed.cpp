#include "CTaskUtilityLineUpPedWithPed.h"

#include <cmath>
#include <CAnimBlendHierarchy.h>

// ============================================================================
// Constructor
// ============================================================================
CTaskUtilityLineUpPedWithPed::CTaskUtilityLineUpPedWithPed(
    const CVector& startOffset,
    const CVector& endOffset,
    float reachEndProgress
)
    : m_vStartOffset(startOffset)
    , m_vEndOffset(endOffset)
    , m_fReachEndProgress(reachEndProgress)
    , m_fLastProgress(0.0f)
    , m_bPositionLocked(false)
{
}

// ============================================================================
// Reset - allow interpolation again
// ============================================================================
void CTaskUtilityLineUpPedWithPed::Reset()
{
    m_fLastProgress = 0.0f;
    m_bPositionLocked = false;
}

// ============================================================================
// GetLocalOffset - Calculate local offset based on animation progress
// ============================================================================
CVector CTaskUtilityLineUpPedWithPed::GetLocalOffset(float animProgress) const
{
    // If position is locked, always return end offset
    if (m_bPositionLocked)
    {
        return m_vEndOffset;
    }
    
    // Clamp progress to valid range
    float progress = animProgress;
    if (progress < 0.0f) progress = 0.0f;
    if (progress > 1.0f) progress = 1.0f;
    
    // If past reach end progress, lock to end position
    if (progress >= m_fReachEndProgress)
    {
        return m_vEndOffset;
    }
    
    // Calculate interpolation factor (0 to 1 within reach phase)
    // Maps [0, reachEndProgress] to [0, 1]
    float t = progress / m_fReachEndProgress;
    
    // Smooth step interpolation for more natural movement
    // smoothstep(t) = 3t^2 - 2t^3
    float smoothT = t * t * (3.0f - 2.0f * t);
    
    // Linearly interpolate between start and end offsets
    CVector result;
    result.x = m_vStartOffset.x + (m_vEndOffset.x - m_vStartOffset.x) * smoothT;
    result.y = m_vStartOffset.y + (m_vEndOffset.y - m_vStartOffset.y) * smoothT;
    result.z = m_vStartOffset.z + (m_vEndOffset.z - m_vStartOffset.z) * smoothT;
    
    return result;
}

// ============================================================================
// GetWorldPosition - Calculate world position for victim
// ============================================================================
CVector CTaskUtilityLineUpPedWithPed::GetWorldPosition(CPed* grabber, float animProgress) const
{
    if (!grabber)
    {
        return CVector(0.0f, 0.0f, 0.0f);
    }
    
    // Get local offset based on animation progress
    CVector localOffset = GetLocalOffset(animProgress);
    
    // Get grabber's position and heading
    CVector grabberPos = grabber->GetPosition();
    float heading = grabber->m_fCurrentRotation;
    
    // Calculate direction vectors from heading
    // Note: GTA SA uses heading where 0 = North (+Y), increases clockwise
    float sinH = sinf(heading);
    float cosH = cosf(heading);
    
    // Transform local offset to world space
    // Forward direction is (-sin, cos) in GTA SA
    // Right direction is (cos, sin)
    CVector worldPos;
    worldPos.x = grabberPos.x + (-sinH * localOffset.y) + (cosH * localOffset.x);
    worldPos.y = grabberPos.y + (cosH * localOffset.y) + (sinH * localOffset.x);
    worldPos.z = grabberPos.z + localOffset.z;
    
    return worldPos;
}

// ============================================================================
// ProcessPed - Main positioning function
// ============================================================================
bool CTaskUtilityLineUpPedWithPed::ProcessPed(CPed* victim, CPed* grabber, CAnimBlendAssociation* anim)
{
    if (!victim || !grabber)
    {
        return false;
    }
    
    // Calculate animation progress
    float animProgress = 0.0f;
    if (anim && anim->m_pHierarchy && anim->m_pHierarchy->m_fTotalTime > 0.0f)
    {
        animProgress = anim->m_fCurrentTime / anim->m_pHierarchy->m_fTotalTime;
    }
    else if (m_bPositionLocked)
    {
        // No animation but position is locked - use end position
        animProgress = 1.0f;
    }
    
    // Check if we should lock position (animation reached end of reach phase)
    if (animProgress >= m_fReachEndProgress && !m_bPositionLocked)
    {
        m_bPositionLocked = true;
    }
    
    // Calculate world position
    CVector worldPos = GetWorldPosition(grabber, animProgress);
    
    // Set victim position
    victim->SetPosn(worldPos);
    
    // Set victim facing direction (opposite to grabber)
    float victimHeading = grabber->m_fCurrentRotation + 3.14159265f;
    
    // Normalize heading to [-PI, PI]
    while (victimHeading > 3.14159265f) victimHeading -= 6.28318530f;
    while (victimHeading < -3.14159265f) victimHeading += 6.28318530f;
    
    victim->m_fCurrentRotation = victimHeading;
    victim->m_fAimingRotation = victimHeading;
    
    // Store last progress for debugging
    m_fLastProgress = animProgress;
    
    return true;
}
