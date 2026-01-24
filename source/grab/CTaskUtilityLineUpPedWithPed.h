#pragma once

#include <plugin.h>
#include <CPed.h>
#include <CAnimBlendAssociation.h>
#include <CVector.h>

// ============================================================================
// CTaskUtilityLineUpPedWithPed
// 
// Utility class for positioning a ped (victim) relative to another ped (grabber)
// during an animation sequence. Interpolates position based on animation progress.
// 
// Modeled after R*'s CTaskUtilityLineUpPedWithCar.
// ============================================================================

class CTaskUtilityLineUpPedWithPed
{
public:
    // ========== Configuration ==========
    
    // Offset from grabber at start of animation (before grab connects)
    CVector m_vStartOffset;
    
    // Offset from grabber at end of animation (holding position)
    CVector m_vEndOffset;
    
    // Animation progress where "reaching" phase ends (0.0 - 1.0)
    // Before this: interpolate from start to end
    // After this: lock at end position
    float m_fReachEndProgress;
    
    // Cached values for interpolation
    float m_fLastProgress;
    bool m_bPositionLocked;

public:
    // ========== Constructor ==========
    CTaskUtilityLineUpPedWithPed(
        const CVector& startOffset,
        const CVector& endOffset,
        float reachEndProgress = 0.95f
    );
    
    ~CTaskUtilityLineUpPedWithPed() = default;

    // ========== Core Methods ==========
    
    // Calculate the local position offset based on animation progress
    // Returns offset vector in grabber's local space
    CVector GetLocalOffset(float animProgress) const;
    
    // Calculate world position for victim based on grabber position and animation
    CVector GetWorldPosition(CPed* grabber, float animProgress) const;
    
    // Main processing function - positions the victim relative to grabber
    // Call this from SetPedPosition or ProcessPed
    // Returns true if position was set
    bool ProcessPed(CPed* victim, CPed* grabber, CAnimBlendAssociation* anim);
    
    // Force lock position to end offset (for when grab animation completes)
    void LockPosition() { m_bPositionLocked = true; }
    
    // Reset to allow interpolation again
    void Reset();

    // ========== Setters ==========
    void SetStartOffset(const CVector& offset) { m_vStartOffset = offset; }
    void SetEndOffset(const CVector& offset) { m_vEndOffset = offset; }
    void SetReachEndProgress(float progress) { m_fReachEndProgress = progress; }
    
    // ========== Getters ==========
    const CVector& GetStartOffset() const { return m_vStartOffset; }
    const CVector& GetEndOffset() const { return m_vEndOffset; }
    float GetReachEndProgress() const { return m_fReachEndProgress; }
    bool IsPositionLocked() const { return m_bPositionLocked; }
};
