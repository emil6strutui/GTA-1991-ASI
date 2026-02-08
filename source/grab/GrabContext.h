#pragma once

#include <plugin.h>
#include <CPed.h>
#include <CTimer.h>
#include <memory>
#include <atomic>

/**
 * Shared context for grab system - enables safe cross-task communication
 * without dangling pointers. Both CTaskComplexGrab and CTaskComplexGrabbed
 * hold a shared_ptr to the same context.
 * 
 * This solves the dangling pointer problem: if either task is destroyed,
 * the other can detect this via IsValid() without accessing freed memory.
 */
class CGrabContext : public std::enable_shared_from_this<CGrabContext>
{
public:
    enum class eGrabPhase : uint8_t
    {
        REACHING,       // Grabber reaching for victim
        HOLDING,        // Stable hold state
        ACTION,         // Performing an action (jab, etc.)
        RELEASING,      // Releasing victim
        FINISHED        // Grab sequence complete
    };

    enum class eGrabAction : uint8_t
    {
        NONE,
        JAB,
        THROW,
        UPPERCUT
    };

    // Task type IDs - must be unique
    static constexpr eTaskType TASK_COMPLEX_GRAB = static_cast<eTaskType>(9001);
    static constexpr eTaskType TASK_COMPLEX_GRABBED = static_cast<eTaskType>(9002);
    static constexpr eTaskType TASK_SIMPLE_GRAB_REACH = static_cast<eTaskType>(9003);
    static constexpr eTaskType TASK_SIMPLE_GRAB_HOLD = static_cast<eTaskType>(9004);
    static constexpr eTaskType TASK_SIMPLE_GRAB_ACTION = static_cast<eTaskType>(9005);
    static constexpr eTaskType TASK_SIMPLE_GRABBED_REACH = static_cast<eTaskType>(9006);
    static constexpr eTaskType TASK_SIMPLE_GRABBED_HELD = static_cast<eTaskType>(9007);
    static constexpr eTaskType TASK_SIMPLE_GRABBED_HIT = static_cast<eTaskType>(9008);

private:
    CPed* m_pGrabber = nullptr;
    CPed* m_pVictim = nullptr;
    
    eGrabPhase m_phase = eGrabPhase::REACHING;
    eGrabAction m_pendingAction = eGrabAction::NONE;
    eGrabAction m_currentAction = eGrabAction::NONE;
    
    float m_fInitialDistance = 0.0f;
    float m_fAnimationSkip = 0.0f;
    
    uint32_t m_nStartTime = 0;
    uint32_t m_nTimeoutMs = 10000;  // 10 second timeout
    
    bool m_bGrabberActive = false;
    bool m_bVictimActive = false;
    bool m_bAborted = false;
    bool m_bGrabberReachComplete = false;
    bool m_bVictimReachComplete = false;
    bool m_bHitConnected = false;
    bool m_bGrabberActionComplete = false;
    bool m_bVictimActionComplete = false;

public:
    CGrabContext() : m_nStartTime(CTimer::m_snTimeInMilliseconds) {}
    
    ~CGrabContext() {
        // Clean up entity references
        if (m_pGrabber) {
            m_pGrabber->m_pEntityIgnoredCollision = nullptr;
            m_pGrabber->CleanUpOldReference(reinterpret_cast<CEntity**>(&m_pGrabber));
        }
        if (m_pVictim) {
            m_pVictim->m_pEntityIgnoredCollision = nullptr;
            m_pVictim->CleanUpOldReference(reinterpret_cast<CEntity**>(&m_pVictim));
        }
    }

    // Factory method
    static std::shared_ptr<CGrabContext> Create(CPed* grabber, CPed* victim, float distance) {
        auto ctx = std::make_shared<CGrabContext>();
        ctx->Initialize(grabber, victim, distance);
        return ctx;
    }

    void Initialize(CPed* grabber, CPed* victim, float distance) {
        m_pGrabber = grabber;
        m_pVictim = victim;
        m_fInitialDistance = distance;
        m_fAnimationSkip = CalculateAnimSkip(distance);
        
        if (grabber) {
            grabber->RegisterReference(reinterpret_cast<CEntity**>(&m_pGrabber));
        }
        if (victim) {
            victim->RegisterReference(reinterpret_cast<CEntity**>(&m_pVictim));
        }
        
        // Set up collision ignore
        if (grabber && victim) {
            grabber->m_pEntityIgnoredCollision = victim;
            victim->m_pEntityIgnoredCollision = grabber;
        }
    }

    // Check if context is still valid for operation
    [[nodiscard]] bool IsValid() const {
        if (m_bAborted) return false;
        if (!m_pGrabber || m_pGrabber->m_fHealth <= 0.0f) return false;
        if (!m_pVictim || m_pVictim->m_fHealth <= 0.0f) return false;
        if (m_pVictim->m_pVehicle) return false;  // Victim entered vehicle
        return true;
    }

    [[nodiscard]] bool IsTimedOut() const {
        return (CTimer::m_snTimeInMilliseconds - m_nStartTime) > m_nTimeoutMs;
    }

    // Phase management
    [[nodiscard]] eGrabPhase GetPhase() const { return m_phase; }
    
    void SetPhase(eGrabPhase phase) { 
        m_phase = phase; 
    }

    void TransitionToHolding() {
        if (m_bGrabberReachComplete && m_bVictimReachComplete) {
            m_phase = eGrabPhase::HOLDING;
        }
    }

    // Hit signal - grabber signals when the punch connects, victim consumes it
    void SignalHitConnected() { m_bHitConnected = true; }
    bool ConsumeHitConnected() {
        if (m_bHitConnected) {
            m_bHitConnected = false;
            return true;
        }
        return false;
    }

    // Action management
    void RequestAction(eGrabAction action) {
        if (m_phase == eGrabPhase::HOLDING) {
            m_pendingAction = action;
            m_phase = eGrabPhase::ACTION;
            m_bGrabberActionComplete = false;
            m_bVictimActionComplete = false;
        }
    }

    [[nodiscard]] eGrabAction ConsumePendingAction() {
        eGrabAction action = m_pendingAction;
        m_pendingAction = eGrabAction::NONE;
        m_currentAction = action;
        return action;
    }

    [[nodiscard]] eGrabAction GetCurrentAction() const { return m_currentAction; }

    // Dual action-completion tracking.
    // Phase stays ACTION until BOTH sides finish their animation.
    // This prevents the grabber from starting a new jab while the
    // victim's hit-reaction is still playing.
    void OnGrabberActionComplete() {
        m_bGrabberActionComplete = true;
        TryCompleteAction();
    }

    void OnVictimActionComplete() {
        m_bVictimActionComplete = true;
        TryCompleteAction();
    }

    // Reach completion tracking
    void OnGrabberReachComplete() { 
        m_bGrabberReachComplete = true; 
        TransitionToHolding();
    }
    
    void OnVictimReachComplete() { 
        m_bVictimReachComplete = true; 
        TransitionToHolding();
    }

    [[nodiscard]] bool IsGrabberReachComplete() const { return m_bGrabberReachComplete; }
    [[nodiscard]] bool IsVictimReachComplete() const { return m_bVictimReachComplete; }

    // Task registration - for detecting if partner task died
    void SetGrabberActive(bool active) { m_bGrabberActive = active; }
    void SetVictimActive(bool active) { m_bVictimActive = active; }
    [[nodiscard]] bool IsGrabberActive() const { return m_bGrabberActive; }
    [[nodiscard]] bool IsVictimActive() const { return m_bVictimActive; }

    // Abort handling
    void Abort() { 
        m_bAborted = true; 
        m_phase = eGrabPhase::FINISHED;
    }
    [[nodiscard]] bool IsAborted() const { return m_bAborted; }

    // Release handling
    void Release() {
        m_phase = eGrabPhase::RELEASING;
    }

    // Getters
    [[nodiscard]] CPed* GetGrabber() const { return m_pGrabber; }
    [[nodiscard]] CPed* GetVictim() const { return m_pVictim; }
    [[nodiscard]] float GetInitialDistance() const { return m_fInitialDistance; }
    [[nodiscard]] float GetAnimationSkip() const { return m_fAnimationSkip; }

private:
    void TryCompleteAction() {
        if (m_bGrabberActionComplete && m_bVictimActionComplete) {
            m_currentAction = eGrabAction::NONE;
            if (m_phase == eGrabPhase::ACTION) {
                m_phase = eGrabPhase::HOLDING;
            }
        }
    }

    static constexpr float GRAB_RANGE = 1.5f;
    static constexpr float REACH_END_PROGRESS = 0.6f;

    [[nodiscard]] static float CalculateAnimSkip(float distance) {
        float clampedDist = std::clamp(distance, 0.0f, GRAB_RANGE);
        float distanceRatio = clampedDist / GRAB_RANGE;
        float skipRatio = 1.0f - distanceRatio;
        return skipRatio * REACH_END_PROGRESS;
    }
};

using GrabContextPtr = std::shared_ptr<CGrabContext>;
