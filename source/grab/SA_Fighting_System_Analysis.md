# San Andreas Fighting System Analysis

This document provides a comprehensive technical breakdown of the fighting, task, and animation systems in GTA San Andreas, based on analysis of the gta-reversed project.

**Purpose:** Reference for implementing VCS-style grab system using proper CTask architecture.

---

## Table of Contents

### Part 1: Task System Architecture
1. [Task System Overview](#task-system-overview)
2. [CTask Base Classes](#ctask-base-classes)
3. [CTaskComplex - The Three Key Methods](#ctaskcomplex---the-three-key-methods)
4. [CTaskManager - Task Execution](#ctaskmanager---task-execution)
5. [Task Slots (Primary & Secondary)](#task-slots-primary--secondary)

### Part 2: Combat Task System
6. [Combat Task Hierarchy](#combat-task-hierarchy)
7. [CTaskSimpleFight - Core Melee Task](#ctasksimplefight---core-melee-task)
8. [CTaskSimpleFightingControl - AI Brain](#ctasksimplefightingcontrol---ai-brain)
9. [CTaskComplexKillPedOnFootMelee - State Machine Example](#ctaskcomplexkillpedonfoot---state-machine-example)

### Part 3: Animation System
10. [Animation System Core](#animation-system-core)
11. [Animation Flags](#animation-flags)
12. [CAnimManager::BlendAnimation](#canimmanagerblendanimation)

### Part 4: Event System
13. [Event-Driven Combat](#event-driven-combat)
14. [CEventDamage Flow](#ceventdamage-flow)

### Part 5: Reference
15. [Key Function Addresses](#key-function-addresses)
16. [Important Modding Notes](#important-modding-notes)
17. [Grab System Implementation Guide](#grab-system-implementation-guide)

---

# Part 1: Task System Architecture

## Task System Overview

GTA SA uses a **hierarchical task-based AI system** where:
- **Complex tasks** handle high-level goals ("kill this ped", "flee from danger")
- **Simple tasks** do actual work ("play punch animation", "walk to point")
- **Events** trigger task creation ("I got hit" -> create fight response task)

```
CTaskComplex (parent goal)
    └── CTaskComplex (sub-goal)
            └── CTaskSimple (actual work - ProcessPed() called each frame)
```

**Key files in gta-reversed:**
```
source/game_sa/Tasks/
├── Task.h                    # Base CTask class
├── TaskComplex.h             # CTaskComplex base
├── TaskSimple.h              # CTaskSimple base
├── TaskManager.h             # Manages ped's task slots
└── TaskTypes/                # All specific task implementations
    ├── TaskSimpleFight.h
    ├── TaskSimpleFightingControl.h
    ├── TaskComplexKillPedOnFoot.h
    └── ...
```

---

## CTask Base Classes

### CTask - Abstract Base (Task.h)

```cpp
class CTask {
protected:
    CTaskComplex* m_Parent{};  // Parent task (only complex tasks can be parents)
    
public:
    // === PURE VIRTUAL - Must implement ===
    virtual CTask* Clone() const = 0;           // Create a copy
    virtual CTask* GetSubTask() const = 0;      // Get subtask (nullptr for simple)
    virtual bool IsSimple() const = 0;          // true = CTaskSimple
    virtual eTaskType GetTaskType() const = 0;  // Return task type enum
    virtual bool MakeAbortable(CPed* ped, eAbortPriority priority, const CEvent* event) = 0;
    
    // === Helper ===
    CTaskComplex* GetParent() { return m_Parent; }
};
```

### CTaskSimple - Terminal Tasks (TaskSimple.h)

Simple tasks are the "leaves" of the task tree - they do actual work.

```cpp
class CTaskSimple : public CTask {
public:
    // Final overrides
    CTask* GetSubTask() const final { return nullptr; }  // No subtasks!
    bool IsSimple() const final { return true; }
    
    // === THE KEY METHOD ===
    virtual bool ProcessPed(CPed* ped) = 0;  // Return TRUE when FINISHED
    
    // Optional: control ped position during task
    virtual bool SetPedPosition(CPed* ped) { return false; }
};
```

**ProcessPed() return value:**
- `false` = Task still running, call again next frame
- `true` = Task FINISHED, remove it and trigger parent's CreateNextSubTask()

### CTaskComplex - Container Tasks (TaskComplex.h)

Complex tasks manage a sub-task and control state transitions.

```cpp
class CTaskComplex : public CTask {
protected:
    CTask* m_pSubTask{};  // Current sub-task (can be Simple OR Complex)
    
public:
    // Final overrides
    CTask* GetSubTask() const final { return m_pSubTask; }
    bool IsSimple() const final { return false; }
    
    // === THE THREE KEY VIRTUAL METHODS ===
    virtual CTask* CreateNextSubTask(CPed* ped) = 0;   // When subtask finishes
    virtual CTask* CreateFirstSubTask(CPed* ped) = 0;  // At task start
    virtual CTask* ControlSubTask(CPed* ped) = 0;      // EVERY FRAME
    
    // Helper to swap subtasks
    void SetSubTask(CTask* subTask) {
        if (m_pSubTask == subTask) return;
        delete std::exchange(m_pSubTask, subTask);
        if (subTask) subTask->m_Parent = this;
    }
};
```

---

## CTaskComplex - The Three Key Methods

### CreateFirstSubTask() - Initialization

**When called:** Once, immediately after task is assigned via `CTaskManager::AddSubTasks`

**Purpose:** Create the initial subtask to begin execution

**Returns:**
- New task pointer = Start with this subtask
- `nullptr` = Task already done (no work needed)

```cpp
// Example from CTaskComplexKillPedOnFootMelee:
CTask* CTaskComplexKillPedOnFootMelee::CreateFirstSubTask(CPed* ped) {
    if (!m_target) {
        return nullptr;  // No target = done immediately
    }
    
    // Decide initial state based on distance
    if (IsTargetInRange(ped, m_arriveRange)) {
        return new CTaskSimpleFightingControl{m_target, ...};  // Close: fight
    }
    return new CTaskComplexSeekEntity{m_target, ...};  // Far: approach
}
```

### CreateNextSubTask() - State Transitions

**When called:** After current subtask FINISHES (ProcessPed returns true, or subtask chain ends)

**Purpose:** Determine what to do next based on what just finished

**How to know what finished:** Check `m_pSubTask->GetTaskType()`

**Returns:**
- New task pointer = Continue with this subtask
- `nullptr` = This complex task is DONE

```cpp
// Example: State machine pattern
CTask* CTaskComplexKillPedOnFootMelee::CreateNextSubTask(CPed* ped) {
    if (!m_target) return nullptr;  // Target gone = done
    
    switch (m_pSubTask->GetTaskType()) {
    case TASK_COMPLEX_SEEK_ENTITY:
        // Finished approaching -> start fighting
        return new CTaskSimpleFightingControl{m_target, ...};
        
    case TASK_SIMPLE_FIGHT_CTRL:
        // Finished fighting -> pause or seek again
        if (IsTargetInRange(ped, m_arriveRange)) {
            return new CTaskSimplePause{};
        }
        return new CTaskComplexSeekEntity{m_target, ...};
        
    case TASK_SIMPLE_PAUSE:
        // Timeout check
        if (CTimer::GetTimeInMS() - m_pauseTimer > 3000) {
            return nullptr;  // Done waiting, end task
        }
        return new CTaskSimpleFightingControl{m_target, ...};
    }
    return nullptr;
}
```

### ControlSubTask() - Per-Frame Control (MOST IMPORTANT!)

**When called:** EVERY FRAME via `CTaskManager::ParentsControlChildren`

**Purpose:**
1. Monitor conditions in real-time
2. Potentially SWAP subtasks mid-execution
3. React to events immediately

**Returns:**
- `m_pSubTask` = Keep current subtask running (no change)
- New task pointer = ABORT current and switch to this
- `nullptr` = THIS task is finished

```cpp
// Example: Reactive combat control
CTask* CTaskComplexKillPedOnFootMelee::ControlSubTask(CPed* ped) {
    // Target died? End task
    if (!m_target || m_target->IsAlive() == false) {
        return nullptr;  // Signal: this task is done
    }
    
    // Helper to abort and switch
    const auto TrySwitch = [&](CTask* newTask) {
        return m_pSubTask->MakeAbortable(ped)
            ? newTask       // Aborted OK, switch
            : m_pSubTask;   // Can't abort, keep current
    };
    
    switch (m_pSubTask->GetTaskType()) {
    case TASK_COMPLEX_SEEK_ENTITY:
        // While approaching, if we get close enough, switch to fighting
        if (IsTargetInRange(ped, m_attackRange)) {
            return TrySwitch(new CTaskSimpleFightingControl{m_target, ...});
        }
        return m_pSubTask;  // Keep seeking
        
    case TASK_SIMPLE_FIGHT_CTRL:
        // While fighting, if target moves away, switch to seeking
        if (!IsTargetInRange(ped, m_attackRange)) {
            return TrySwitch(new CTaskComplexSeekEntity{m_target, ...});
        }
        return m_pSubTask;  // Keep fighting
    }
    
    return m_pSubTask;  // Default: no change
}
```

### Visual: ControlSubTask vs CreateNextSubTask

```
                    ControlSubTask (EVERY FRAME)
                    ─────────────────────────────
                    Checks conditions continuously
                    Can INTERRUPT current subtask
                    Returns m_pSubTask to continue
                    Returns new task to SWITCH
                    Returns nullptr to END this task
                            │
                            ▼
┌─────────────────────────────────────────────────────────────┐
│                     SUBTASK RUNNING                          │
│                                                              │
│  ProcessPed() called each frame until returns true          │
│                                                              │
└─────────────────────────────────────────────────────────────┘
                            │
                    Subtask finishes (ProcessPed returns true)
                            │
                            ▼
                    CreateNextSubTask (ONCE)
                    ────────────────────────
                    Called when subtask COMPLETES
                    Decides what to do NEXT
                    Returns new task to continue
                    Returns nullptr to END this task
```

---

## CTaskManager - Task Execution

### ManageTasks() - Main Loop (called every frame per ped)

```cpp
void CTaskManager::ManageTasks() {
    // 1. Process active primary task tree
    if (auto activeIdx = GetActiveTaskIndex(); activeIdx != -1) {
        auto*& task = m_aPrimaryTasks[activeIdx];
        
        // Process up to 10 iterations (handles rapid task completion)
        for (int i = 0; i < 10; i++) {
            auto result = ProcessTaskTree(task, true);
            
            if (result == TT_FINISHED) {
                delete std::exchange(task, nullptr);
                break;
            }
            if (result != TT_ADVANCED) break;
        }
    }
    
    // 2. Process all secondary tasks
    for (auto& task : m_aSecondaryTasks) {
        if (!task) continue;
        
        ProcessResult res;
        while ((res = ProcessTaskTree(task, false)) == TT_ADVANCED);
        
        if (res == TT_FINISHED) {
            delete std::exchange(task, nullptr);
        }
    }
}
```

### ProcessTaskTree() - Single Frame Execution

```cpp
ProcessResult ProcessTaskTree(CTask* root, bool isPrimary) {
    // 1. Let all parents control children (ControlSubTask calls)
    ParentsControlChildren(root);
    
    // 2. Get deepest task (must be simple)
    auto* leaf = GetLastTaskOf(root);
    if (!leaf->IsSimple()) return TT_FINISHED;  // Error: tree not terminated
    
    // 3. Execute the simple task
    if (!leaf->AsSimple()->ProcessPed(m_pPed)) {
        return TT_SUCCESS;  // Still running
    }
    
    // 4. Simple task finished - create next
    SetNextSubTask(leaf->GetParent());
    
    return root->GetSubTask() ? TT_ADVANCED : TT_FINISHED;
}
```

### ParentsControlChildren() - The ControlSubTask Loop

```cpp
void CTaskManager::ParentsControlChildren(CTask* parent) {
    for (auto* task = parent; task && !task->IsSimple();) {
        auto* complex = task->AsComplex();
        auto* oldSub = complex->GetSubTask();
        
        // Call ControlSubTask - THE MAGIC HAPPENS HERE
        auto* newSub = complex->ControlSubTask(m_pPed);
        
        if (newSub != oldSub) {
            // SWITCH! Abort old, set new
            oldSub->MakeAbortable(m_pPed);
            complex->SetSubTask(newSub);
            AddSubTasks(newSub);  // Initialize new subtask tree
            return;
        }
        
        task = task->GetSubTask();  // Go deeper
    }
}
```

---

## Task Slots (Primary & Secondary)

### Primary Task Slots (by priority)

```cpp
enum ePrimaryTasks {
    TASK_PRIMARY_PHYSICAL_RESPONSE = 0,  // HIGHEST - Hit reactions, ragdoll
    TASK_PRIMARY_EVENT_RESPONSE_TEMP,    // Temporary events (flinch)
    TASK_PRIMARY_EVENT_RESPONSE_NONTEMP, // Persistent events (fight back!)
    TASK_PRIMARY_PRIMARY,                // Main task (mission objective)
    TASK_PRIMARY_DEFAULT,                // LOWEST - Idle, wander
    TASK_PRIMARY_MAX = 5
};
```

**Key insight:** Only the FIRST non-null slot is "active". Lower index = higher priority.

```cpp
CTask* CTaskManager::GetActiveTask() {
    for (auto* task : m_aPrimaryTasks) {
        if (task) return task;  // First non-null wins
    }
    return nullptr;
}
```

### Secondary Task Slots (run alongside primary)

```cpp
enum eSecondaryTask {
    TASK_SECONDARY_ATTACK = 0,    // CTaskSimpleFight lives here!
    TASK_SECONDARY_DUCK,          // Crouching
    TASK_SECONDARY_SAY,           // Speech
    TASK_SECONDARY_FACIAL_COMPLEX,// Facial expressions
    TASK_SECONDARY_PARTIAL_ANIM,  // Upper body anims
    TASK_SECONDARY_IK,            // Inverse kinematics
    TASK_SECONDARY_MAX = 6
};
```

**Important:** Combat uses `TASK_SECONDARY_ATTACK` for the actual fight task, while the primary task handles state (seek/fight/flee).

---

# Part 2: Combat Task System

## Combat Task Hierarchy

```
Event: CEventDamage (got hit!)
    │
    ▼
CEventHandler::ComputeDamageResponse()
    │
    ├─► CTaskComplexFallAndGetUp (PHYSICAL_RESPONSE slot)
    │
    └─► CTaskComplexKillPedOnFoot (EVENT_RESPONSE_NONTEMP slot)
            │
            └─► CTaskComplexKillPedOnFootMelee (for melee combat)
                    │
                    ├─► CTaskComplexSeekEntity (approaching)
                    │
                    └─► CTaskSimpleFightingControl (in range)
                            │
                            └─► Creates CTaskSimpleFight (SECONDARY_ATTACK)
```

### Combat Task Files (gta-reversed)

| Task | File | Purpose |
|------|------|---------|
| `CTaskSimpleFight` | TaskSimpleFight.h/cpp | Core melee - combos, hits, anims |
| `CTaskSimpleFightingControl` | TaskSimpleFightingControl.h/cpp | AI decision making |
| `CTaskSimpleBeHit` | TaskSimpleBeHit.h/cpp | Hit reactions |
| `CTaskComplexKillPedOnFoot` | TaskComplexKillPedOnFoot.h/cpp | Base "kill ped" task |
| `CTaskComplexKillPedOnFootMelee` | TaskComplexKillPedOnFootMelee.h/cpp | Melee state machine |

---

## CTaskSimpleFight - Core Melee Task

### Structure

```cpp
class CTaskSimpleFight : public CTaskSimple {
    // State flags
    bool                   m_bIsFinished;       // Task complete
    bool                   m_bIsInControl;      // Currently in control
    bool                   m_bAnimsReferenced;  // Anims loaded
    
    // Timing
    uint16                 m_nIdlePeriod;       // Max idle time (ms)
    uint16                 m_nIdleCounter;      // Current idle time
    
    // Combat state
    CEntity*               m_pTargetEntity;     // Fight target
    int8                   m_nComboSet;         // Combo set (4-15)
    eFightAttackType       m_nCurrentMove;      // Current attack (0-4)
    uint8                  m_nNextCommand;      // Queued command
    uint8                  m_nLastCommand;      // Previous command
    int8                   m_nContinueStrike;   // Continue attacking
    int8                   m_nChainCounter;     // Chain combo counter
    
    // Animation
    AssocGroupId           m_nRequiredAnimGroup;
    CAnimBlendAssociation* m_pAnim;             // Current attack anim
    CAnimBlendAssociation* m_pIdleAnim;         // Idle stance anim
    
    // Static combo data
    static CMeleeInfo (&m_aComboData)[12];      // At 0xC170D0
};
```

### The 12 Combo Sets

`m_nComboSet` values 4-15 map to array indices 0-11:

| Index | m_nComboSet | Animation Group | Style |
|-------|-------------|-----------------|-------|
| 0 | 4 | ANIM_GROUP_MELEE_1 | Default unarmed (FightA) |
| 1 | 5 | ANIM_GROUP_MELEE_2 | Boxing (FightB) |
| 2 | 6 | ANIM_GROUP_MELEE_3 | Kung Fu (FightC) |
| 3 | 7 | ANIM_GROUP_MELEE_4 | Street (FightD) |
| 4 | 8 | ANIM_GROUP_BBBAT_1 | Baseball bat |
| 5 | 9 | ANIM_GROUP_GCLUB_1 | Golf club |
| 6 | 10 | ANIM_GROUP_KNIFE_1 | Knife |
| 7 | 11 | ANIM_GROUP_SWORD_1 | Katana |
| 8 | 12 | ANIM_GROUP_DILDO_1 | Dildo |
| 9 | 13 | ANIM_GROUP_FLOWERS_1 | Flowers |
| 10 | 14 | ANIM_GROUP_CSAW_1 | Chainsaw |
| 11 | 15 | ANIM_GROUP_KICK_STD | Kick moves |

**To get array index:** `int index = m_nComboSet - 4;`

### CMeleeInfo - Combo Data Structure

```cpp
class CMeleeInfo {
    AssocGroupId m_nAnimGroup;   // Animation group
    float  m_fRanges;            // Attack range
    float  m_fHit[5];            // Hit timing per attack (0.0-1.0 of anim)
    float  m_fChain[5];          // Chain window timing
    float  m_fRadius[5];         // Hit detection radius
    float  m_fGroundLoop;        // Ground attack timing
    int32  m_nDamage;            // Base damage
    int32  m_Hit[5];             // Hit animation IDs
    int32  m_AltHit[5];          // Alternate hit anims
    uint16 m_wFlags;             // Combo flags
};
```

### Attack Types (m_nCurrentMove)

```cpp
enum eFightAttackType : int8 {
    FIGHT_ATTACK_HIT_1 = 0,       // First combo attack
    FIGHT_ATTACK_HIT_2 = 1,       // Second combo attack
    FIGHT_ATTACK_HIT_3 = 2,       // Third combo attack
    FIGHT_ATTACK_FIGHT_BLOCK = 3, // Blocking
    FIGHT_ATTACK_FIGHTIDLE = 4,   // Idle stance
};
```

### Combo Progression

```
Player presses attack
        │
        ▼
m_nCurrentMove = HIT_1
        │
Attack again in chain window?
        │
   YES──┴──NO
    │      │
    ▼      ▼
HIT_2   IDLE
    │
Attack again?
    │
   YES──┴──NO
    │      │
    ▼      ▼
HIT_3   IDLE
    │
    └──────► IDLE (combo complete)
```

### ControlFight() - Command Interface

```cpp
bool ControlFight(CEntity* entity, uint8 command);
```

**Command values (from PlayerOnFoot.cpp):**

| Cmd | Purpose | When Used |
|-----|---------|-----------|
| 0 | Continue/maintain | Default each frame |
| 1 | End fight | Weapon change, exit |
| 11 | Normal attack | Standard melee button |
| 12 | Heavy attack | Strong attack |
| 15 | Exit (standing) | Sprint while still |
| 16 | Exit (moving back) | Sprint + moving backward |

### ProcessPed() - Frame Loop

```cpp
bool CTaskSimpleFight::ProcessPed(CPed* ped) {
    // 1. Check finished flag
    if (m_bIsFinished) {
        CleanupAnims();
        return true;  // DONE
    }
    
    // 2. Load animations if needed
    if (!m_bAnimsReferenced) {
        LoadComboAnims();
        m_bAnimsReferenced = true;
    }
    
    // 3. Process current state
    switch (m_nCurrentMove) {
    case FIGHT_ATTACK_FIGHTIDLE:
        // Increment idle counter
        m_nIdleCounter += CTimer::GetTimeStepInMS();
        if (m_nIdleCounter >= m_nIdlePeriod) {
            m_bIsFinished = true;  // Timeout
        }
        // Check for attack command
        if (m_nContinueStrike) {
            StartAttack(ped);
        }
        break;
        
    case FIGHT_ATTACK_HIT_1:
    case FIGHT_ATTACK_HIT_2:
    case FIGHT_ATTACK_HIT_3:
        // Check hit timing
        float hitTime = m_aComboData[m_nComboSet - 4].m_fHit[m_nCurrentMove];
        if (m_pAnim->m_CurrentTime >= hitTime) {
            FightStrike(ped, strikePos);  // Do hit detection
        }
        break;
    }
    
    // 4. Face target
    if (m_pTargetEntity) {
        UpdateFacing(ped);
    }
    
    return false;  // Continue
}
```

### Hit Detection: FightStrike() and FightHitPed()

```cpp
void FightStrike(CPed* ped, CVector& posn);
void FightHitPed(CPed* creator, CPed* victim, CVector& posn1, CVector& posn2, int16);
```

**Flow:**
1. `FightStrike()` called at hit frame (from `CMeleeInfo::m_fHit[]`)
2. Uses sphere collision with radius from `CMeleeInfo::m_fRadius[]`
3. For each ped hit, calls `FightHitPed()`
4. `FightHitPed()` creates `CEventDamage` on victim
5. Victim's `CEventHandler` processes damage and creates response

---

## CTaskSimpleFightingControl - AI Brain

This task is the **AI decision-making layer** that sits on top of `CTaskSimpleFight`.

### Relationship

| Task | Role |
|------|------|
| CTaskSimpleFightingControl | AI brain - decides WHEN to attack, move, strafe |
| CTaskSimpleFight | Executor - plays animations, does hit detection |

### Structure

```cpp
class CTaskSimpleFightingControl : public CTaskSimple {
    CEntity::Ref m_target;       // Target entity
    float m_maxAttackAngle;      // Attack cone angle
    float m_maxAttackRange;      // Max range to attack
    uint32 m_nextAttackTime;     // When next attack allowed
    uint32 m_someTime;
    bool m_bool;                 // Finish flag
};

constexpr auto FIGHT_CTRL_MAX_ATTACK_ANGLE_RAD = DegreesToRadians(15);  // 15 degrees
constexpr auto FIGHT_CTRL_FIGHT_IDLE_TIME = 60000.f;  // 60 second timeout
```

### CalcMoveCommand() - AI Decision Logic

```cpp
int16 CTaskSimpleFightingControl::CalcMoveCommand(CPed* ped) {
    if (!m_target) return -1;
    
    const auto toTarget = m_target->GetPosition() - ped->GetPosition();
    
    // === ANGLE CHECK ===
    // If not facing target within 15 degrees, delay attack
    float angleDiff = std::abs(toTarget.Heading() - ped->m_fCurrentRotation);
    if (angleDiff >= FIGHT_CTRL_MAX_ATTACK_ANGLE_RAD) {
        m_nextAttackTime += CTimer::GetTimeStepInMS();
        return 0;  // Turn to face
    }
    
    // === DISTANCE CHECK ===
    float range = toTarget.Magnitude() - m_maxAttackRange;
    
    if (range > 0.1f) {
        return 3;  // TOO FAR: Move forward
    }
    
    if (range > -0.1f) {
        // PERFECT RANGE: occasionally strafe
        return (rand() % 16 == 0) ? 7 : -1;  // 6.25% strafe left
    }
    
    // TOO CLOSE: occasionally back up
    if (rand() % 64 == 0) return 8;   // Strafe right
    if (rand() % 64 == 0) return 10;  // Back up
    
    return -1;  // Idle
}
```

**Return values:**
| Value | Meaning |
|-------|---------|
| -1 | Idle / wait |
| 0 | Turn to face |
| 3 | Move forward |
| 7 | Strafe left |
| 8 | Strafe right |
| 10 | Move backward |

### ProcessPed() - Creates CTaskSimpleFight

```cpp
bool CTaskSimpleFightingControl::ProcessPed(CPed* ped) {
    if (!m_target || m_bool) return true;
    
    ped->GiveWeaponAtStartOfFight();
    
    // Create fight task if not exists
    auto* attackTask = ped->GetTaskManager().GetTaskSecondary(TASK_SECONDARY_ATTACK);
    if (!attackTask) {
        ped->GetTaskManager().SetTaskSecondary(
            new CTaskSimpleFight{
                m_target,
                false,  // Initial command = idle
                (uint32)FIGHT_CTRL_FIGHT_IDLE_TIME
            },
            TASK_SECONDARY_ATTACK
        );
        m_nextAttackTime = 0;
    }
    
    // Verify correct task type
    if (attackTask->GetTaskType() != TASK_SIMPLE_FIGHT) {
        attackTask->MakeAbortable(ped);
        return false;
    }
    
    // AI decision making...
    int moveCmd = CalcMoveCommand(ped);
    // Apply movement/attack commands...
    
    return false;
}
```

---

## CTaskComplexKillPedOnFootMelee - State Machine Example

This is the best example of a complex combat task with state transitions.

### State Machine Diagram

```
                    CreateFirstSubTask()
                           │
          ┌────────────────┼────────────────┐
          │                │                │
     (in range)      (out of range)   (bStayInPlace)
          │                │                │
          ▼                ▼                ▼
    FIGHT_CTRL      SEEK_ENTITY          PAUSE
          │                │                │
          └───────┬────────┴────────┬──────┘
                  │                 │
         ControlSubTask() every frame
                  │                 │
         Monitors distance continuously
                  │                 │
    ┌─────────────┼─────────────────┼─────────────┐
    │             │                 │             │
(too far)   (close enough)    (too close)   (target dies)
    │             │                 │             │
    ▼             ▼                 ▼             ▼
SEEK_ENTITY  FIGHT_CTRL          PAUSE        nullptr
                                              (end task)
```

### CreateSubTask() - Factory Method Pattern

```cpp
CTask* CTaskComplexKillPedOnFootMelee::CreateSubTask(eTaskType type, CPed* ped) {
    switch (type) {
    case TASK_SIMPLE_FIGHT_CTRL:
        return new CTaskSimpleFightingControl{m_target, m_attackAngle, m_attackRange};
        
    case TASK_COMPLEX_SEEK_ENTITY:
        return new CTaskComplexSeekEntity<>{m_target, 50000, 1000, ...};
        
    case TASK_SIMPLE_PAUSE:
        return new CTaskSimplePause{};
        
    case TASK_FINISHED:
        return nullptr;
    }
    return nullptr;
}
```

### ControlSubTask() - Reactive State Switching

```cpp
CTask* CTaskComplexKillPedOnFootMelee::ControlSubTask(CPed* ped) {
    ped->Say(CTX_GLOBAL_FIGHT);
    
    // New target?
    if (m_bNewTarget) {
        return CreateFirstSubTask(ped);
    }
    
    // Target gone or dead?
    if (!m_target || m_target->m_fHealth <= 0.f) {
        return nullptr;  // END TASK
    }
    
    // Helper to safely switch
    const auto TrySwitch = [&](eTaskType newType) {
        return m_pSubTask->MakeAbortable(ped)
            ? CreateSubTask(newType, ped)
            : m_pSubTask;
    };
    
    switch (m_pSubTask->GetTaskType()) {
    case TASK_SIMPLE_PAUSE:
    case TASK_COMPLEX_SEEK_ENTITY:
        if (m_target->IsSubmergedInWater()) {
            return TrySwitch(TASK_COMPLEX_SEQUENCE);
        }
        if (!IsTargetInRange(ped, m_arriveRange)) {
            return m_pSubTask;  // Keep seeking
        }
        return TrySwitch(TASK_SIMPLE_FIGHT_CTRL);  // Close! Fight!
        
    case TASK_SIMPLE_FIGHT_CTRL:
        if (!IsTargetInRange(ped, m_attackRange)) {
            return m_pSubTask;  // Keep fighting
        }
        // Too far - switch to seek or pause
        return TrySwitch(ped->bStayInSamePlace
            ? TASK_SIMPLE_PAUSE
            : TASK_COMPLEX_SEEK_ENTITY);
    }
    
    return m_pSubTask;
}
```

---

# Part 3: Animation System

## Animation System Core

### CAnimBlendAssociation

```cpp
class CAnimBlendAssociation {
    CAnimBlendHierarchy* m_BlendHier;     // Animation data
    float m_BlendAmount;                   // Current blend weight (0.0-1.0)
    float m_BlendDelta;                    // Per-frame change (+in, -out)
    float m_CurrentTime;                   // Playback position (seconds)
    float m_Speed;                         // Speed multiplier (1.0 = normal)
    uint16 m_Flags;                        // Animation flags
    uint16 m_AnimId;                       // Animation ID
    
    // Callback
    void (*m_pCallbackFunc)(CAnimBlendAssociation*, void*);
    void* m_pCallbackData;
};
```

### Animation Progress

```cpp
float progress = anim->m_CurrentTime / anim->m_BlendHier->m_fTotalTime;  // 0.0 - 1.0
```

---

## Animation Flags

```cpp
enum eAnimBlendAssocFlag : uint16 {
    ANIMATION_STARTED             = 0x1,    // Has started
    ANIMATION_IS_LOOPED           = 0x2,    // Loop at end
    ANIMATION_FREEZE_LAST_FRAME   = 0x4,    // Stay on last frame
    ANIMATION_UNLOCK_LAST_FRAME   = 0x8,    // Allow movement on last frame
    ANIMATION_IS_PARTIAL          = 0x10,   // Only affects specific bones
    ANIMATION_IS_BLEND_AUTO_REMOVE = 0x40,  // Remove when blend = 0
    ANIMATION_IS_FINISH_AUTO_REMOVE = 0x80, // Remove when done (DANGEROUS!)
    ANIMATION_IS_PLAYING          = 0x100,  // Currently playing
    ANIMATION_CAN_EXTRACT_X       = 0x1000, // Extract root X movement
    ANIMATION_CAN_EXTRACT_Y       = 0x2000, // Extract root Y movement
};
```

### Common Flag Combinations

```cpp
// Standard non-looping
ANIMATION_IS_PLAYING

// Looping idle
ANIMATION_IS_PLAYING | ANIMATION_IS_LOOPED

// Partial body (upper body only)
ANIMATION_IS_PLAYING | ANIMATION_IS_PARTIAL
```

---

## Animation Callbacks (CRITICAL!)

Animation callbacks are essential for knowing when animations finish or are deleted. However, improper handling causes crashes.

### CAnimBlendAssociation Callback Structure

From IDA disassembly, the callback-related fields at offsets:

```cpp
class CAnimBlendAssociation {
    // ... other fields ...
    // +0x30: Callback type (0=none, 1=finish, 2=delete)
    // +0x34: Callback function pointer
    // +0x38: Callback data pointer (passed to callback)
};
```

### SetFinishCallback vs SetDeleteCallback

```cpp
// SetFinishCallback (0x4CEBE0)
// Sets callback type to 1
// Called when animation reaches its end (non-looped animations)
void SetFinishCallback(void (*func)(CAnimBlendAssociation*, void*), void* data);

// SetDeleteCallback (0x4CEBC0)  
// Sets callback type to 2
// Called when animation is deleted for ANY reason (interrupted, blend out, etc.)
// SAFER for victim animations that may be interrupted by combat/damage
void SetDeleteCallback(void (*func)(CAnimBlendAssociation*, void*), void* data);
```

### When Callbacks Are Invoked

**UpdateTime (0x4D13D0)** - For finish callbacks (type=1):
```cpp
// At line 0x4D1454:
if ([esi+30h] == 1) {          // Check callback type
    push [esi+38h]              // Push data
    push esi                    // Push anim
    mov [esi+30h], 0            // Clear type BEFORE calling
    call [esi+34h]              // Call callback
    // Then resets to DefaultAnimCB with data=0
}
```

**UpdateBlend (0x4D1490)** - For delete callbacks (type=2):
```cpp
// At line 0x4D14E9:
if ([esi+30h] == 2 || [esi+30h] == 1) {
    push [esi+38h]              // Push data
    push esi                    // Push anim
    call [esi+34h]              // Call callback
    // Then resets to DefaultAnimCB with data=0
}
```

### CRITICAL: Never Use nullptr for Callbacks!

The game ALWAYS calls the callback function - it doesn't check for null. After invoking a callback, the game resets it to `CDefaultAnimCallback::DefaultAnimCB` (0x4D1390) with data=0.

**WRONG - WILL CRASH:**
```cpp
// This crashes because game calls nullptr as a function!
m_pAnim->SetFinishCallback(nullptr, nullptr);
```

**WRONG - WILL ALSO CRASH:**
```cpp
// Using wrong address (0x4D6190 is inside RpAnimBlendPluginAttach, not a callback!)
static const auto DefaultAnimCB = (void(*)(CAnimBlendAssociation*, void*))0x4D6190;
m_pAnim->SetFinishCallback(DefaultAnimCB, nullptr);  // Crashes!
```

**CORRECT - Use your own no-op callback:**
```cpp
// Define a truly empty callback that ignores all parameters
static void NoOpAnimCallback(CAnimBlendAssociation*, void*)
{
    // Intentionally empty - does nothing, doesn't touch any pointers
}

// Use it when detaching callbacks
m_pAnim->SetFinishCallback(NoOpAnimCallback, nullptr);
m_pAnim->SetDeleteCallback(NoOpAnimCallback, nullptr);
```

### Safe Callback Pattern for Tasks

```cpp
class CTaskSimpleMyAnim : public CTaskSimple {
    CAnimBlendAssociation* m_pAnim = nullptr;
    
    // Static no-op callback - MUST be defined
    static void NoOpAnimCallback(CAnimBlendAssociation*, void*) {}
    
    // Actual callback
    static void OnAnimFinished(CAnimBlendAssociation* anim, void* data) {
        auto* task = static_cast<CTaskSimpleMyAnim*>(data);
        if (!task) return;
        
        // CRITICAL: Clear pointer FIRST - anim may be deleted after callback
        task->m_pAnim = nullptr;
        
        // Then update state
        task->m_bFinished = true;
    }
    
    ~CTaskSimpleMyAnim() {
        if (m_pAnim) {
            // CRITICAL: Detach callback BEFORE blend out
            m_pAnim->SetFinishCallback(NoOpAnimCallback, nullptr);
            m_pAnim->m_fBlendDelta = -4.0f;
            m_pAnim = nullptr;
        }
    }
    
    bool MakeAbortable(CPed* ped, eAbortPriority priority, CEvent* event) override {
        if (m_pAnim) {
            m_pAnim->SetFinishCallback(NoOpAnimCallback, nullptr);
            m_pAnim->m_fBlendDelta = -8.0f;  // Fast blend out
            m_pAnim = nullptr;
        }
        return true;
    }
    
    void StartAnimation(CPed* ped) {
        m_pAnim = CAnimManager::BlendAnimation(ped->m_pRwClump, ...);
        if (m_pAnim) {
            m_pAnim->SetFinishCallback(OnAnimFinished, this);
        }
    }
};
```

### Choosing Between SetFinishCallback and SetDeleteCallback

| Use Case | Recommended Callback |
|----------|---------------------|
| Player-controlled animation | SetFinishCallback |
| Animation that should complete | SetFinishCallback |
| Victim/NPC animation (can be interrupted) | SetDeleteCallback |
| Animation that may be overridden by combat | SetDeleteCallback |

---

## CAnimManager::BlendAnimation

```cpp
// Address: 0x4D4610
CAnimBlendAssociation* CAnimManager::BlendAnimation(
    RpClump* clump,           // Ped's clump
    AssocGroupId groupId,     // Animation group
    AnimationId animId,       // Animation ID
    float blendDelta = 4.0f   // Blend speed
);
```

### Behavior

1. Creates new animation with blend = 0.0
2. Sets positive blend delta (fades in)
3. Conflicts existing anims in same group (sets negative delta - fades out)
4. Does NOT instantly switch - smooth transition

### Blend Delta Values

| Value | Effect |
|-------|--------|
| 4.0 | Standard (~0.25 sec) |
| 8.0 | Fast (~0.125 sec) |
| 16.0 | Very fast (~0.06 sec) |
| 32.0 | Near-instant, overrides combat |
| 1000.0 | Effectively instant |

---

# Part 4: Event System

## Event-Driven Combat

The event system triggers task creation:

```
Game Event (damage, threat, etc.)
        │
        ▼
CEvent created, added to ped's CEventGroup
        │
        ▼
CEventHandler::HandleEvents() processes highest priority
        │
        ▼
ComputeEventResponseTask() creates response
        │
        ▼
Tasks assigned to appropriate slots
```

### Key Event Files

```
source/game_sa/Events/
├── Event.h              # Base CEvent
├── EventDamage.h        # Damage events
├── EventHandler.h       # Processes events
└── EventGroup.h         # Event queue per ped
```

---

## CEventDamage Flow

### Structure

```cpp
class CEventDamage : public CEventEditableResponse {
    CEntity*           m_pSourceEntity;     // Attacker
    eWeaponType        m_weaponType;        // Weapon used
    ePedPieceTypes     m_pedPieceType;      // Body part hit
    uint8              m_ucDirection;       // Attack direction
    bool               m_bFallDown;         // Should fall?
    CPedDamageResponse m_damageResponse;    // Calculated response
    AssocGroupId       m_nAnimGroup;        // Reaction anim group
    AnimationId        m_nAnimID;           // Reaction anim
};
```

### ComputeDamageResponse() - Fight Response Logic

```cpp
void CEventHandler::ComputeDamageResponse(CEventDamage* e, ...) {
    // Death?
    if (e->HasKilledPed()) {
        return new CTaskComplexDie{...};
    }
    
    // Physical response (hit animation)
    if (e->m_bFallDown) {
        m_PhysicalResponseTask = new CTaskComplexFallAndGetUp{...};
    } else {
        m_PhysicalResponseTask = new CTaskSimpleBeHit{...};
    }
    
    // Combat response
    if (e->m_pSourceEntity && e->m_pSourceEntity->IsPed()) {
        ComputePersonalityResponseToDamage(e, attacker);
    }
}

void ComputePersonalityResponseToDamage(...) {
    // Based on ped's decision maker (personality)
    if (shouldFight) {
        m_EventResponseTask = new CTaskComplexKillPedOnFoot{attacker};
    } else {
        m_EventResponseTask = new CTaskComplexSmartFleeEntity{attacker};
    }
}
```

### Complete Combat Flow

```
1. Player hits NPC (CTaskSimpleFight::FightHitPed)
        │
        ▼
2. CEventDamage created, added to NPC's EventGroup
        │
        ▼
3. CEventHandler::HandleEvents() called for NPC
        │
        ▼
4. ComputeEventResponseTask() -> ComputeDamageResponse()
        │
        ├─► CTaskComplexFallAndGetUp -> PHYSICAL_RESPONSE slot
        │
        └─► CTaskComplexKillPedOnFoot -> EVENT_RESPONSE_NONTEMP slot
                │
                └─► Creates CTaskComplexKillPedOnFootMelee
                        │
                        └─► Creates CTaskSimpleFightingControl
                                │
                                └─► Creates CTaskSimpleFight in SECONDARY_ATTACK
                                        │
                                        ▼
                                NPC is now fighting back!
```

---

# Part 5: Reference

## Key Function Addresses

### Animation System
| Function | Address |
|----------|---------|
| CAnimManager::BlendAnimation | 0x4D4610 |
| CAnimManager::AddAnimation | 0x4D3AA0 |
| CAnimBlendAssociation::SetFinishCallback | 0x4CEBE0 |
| CAnimBlendAssociation::SetDeleteCallback | 0x4CEBC0 |
| CAnimBlendAssociation::UpdateTime | 0x4D13D0 |
| CAnimBlendAssociation::UpdateBlend | 0x4D1490 |
| CDefaultAnimCallback::DefaultAnimCB | 0x4D1390 |
| RpAnimBlendClumpGetAssociation | 0x4D6870 |

### Combat System
| Function | Address |
|----------|---------|
| CTaskSimpleFight::ProcessPed | 0x61C000 |
| CTaskSimpleFight::ControlFight | (not reversed) |
| CTaskSimpleFightingControl::ProcessPed | 0x62A0A0 |
| CTaskSimpleFightingControl::CalcMoveCommand | 0x624B50 |
| CTaskSimpleBeHit::ProcessPed | 0x620090 |

### Event System
| Function | Address |
|----------|---------|
| CEventHandler::ComputeDamageResponse | 0x4C0170 |
| CEventDamage::AffectsPed | (inline) |

### Combo Data
| Data | Address |
|------|---------|
| CMeleeInfo m_aComboData[12] | 0xC170D0 |

---

## Important Modding Notes

### Animation Best Practices

1. **DO use BlendAnimation** - handles transitions
2. **DO NOT force m_BlendAmount = 1.0f** - causes corruption
3. **DO NOT use ANIMATION_IS_FINISH_AUTO_REMOVE with callbacks** - crashes!
4. **DO use high blend delta (32+)** to override combat anims
5. **NEVER set callback to nullptr** - game always calls it, will crash!
6. **ALWAYS detach callbacks before cleanup** - prevents dangling pointer crashes

### Callback Safety

```cpp
// Define your own no-op callback (REQUIRED)
static void NoOpAnimCallback(CAnimBlendAssociation*, void*) {}

// SAFE - Using callbacks:
anim->m_Flags |= ANIMATION_IS_BLEND_AUTO_REMOVE;
anim->SetFinishCallback(MyCallback, userData);

// SAFE - Detaching callbacks before cleanup:
anim->SetFinishCallback(NoOpAnimCallback, nullptr);  // Detach FIRST
anim->m_fBlendDelta = -4.0f;                         // Then blend out

// UNSAFE - WILL CRASH (nullptr callback):
anim->SetFinishCallback(nullptr, nullptr);  // Game calls nullptr!

// UNSAFE - WILL CRASH (auto-remove with callback):
anim->m_Flags |= ANIMATION_IS_FINISH_AUTO_REMOVE;  // Deletes before callback!
anim->SetFinishCallback(MyCallback, userData);

// UNSAFE - Wrong cleanup order:
anim->m_fBlendDelta = -4.0f;  // Animation deleted...
// ... callback fires with stale data -> crash
```

### Callback Implementation Checklist

1. **Define a static no-op callback** in your task class
2. **In destructor**: Detach callback with no-op, then blend out
3. **In MakeAbortable**: Same pattern as destructor
4. **In callback function**: Clear `m_pAnim = nullptr` FIRST, then update state
5. **Use SetDeleteCallback** for animations that can be interrupted (victim anims)

### Victim Animation Override

```cpp
// Use high blend delta to override AI combat anims
CAnimBlendAssociation* anim = CAnimManager::BlendAnimation(
    victimPed->m_pRwClump,
    ANIM_GROUP_CUSTOM,
    ANIM_ID_GRABBED,
    32.0f  // High delta overrides blocking anims
);
```

### Entity Reference Safety

```cpp
// Register reference (prevents dangling pointer)
CEntity::SafeRegisterRef(m_target);

// Clean up when done
CEntity::SafeCleanUpRef(m_target);
```

---

## Grab System Implementation Guide

Based on this analysis, here's the recommended architecture for the VCS-style grab system:

### Task Classes to Create

```cpp
// 1. Complex task for grabber (player)
class CTaskComplexGrab : public CTaskComplex {
    CPed* m_pVictim;
    eGrabState m_state;
    
    CTask* CreateFirstSubTask(CPed* ped);    // -> CTaskSimpleGrabInit
    CTask* CreateNextSubTask(CPed* ped);     // State transitions
    CTask* ControlSubTask(CPed* ped);        // Check victim escape, input
};

// 2. Simple task for grab actions
class CTaskSimpleGrabAction : public CTaskSimple {
    eGrabActionType m_action;  // JAB, THROW, UPPERCUT, etc.
    
    bool ProcessPed(CPed* ped);  // Play anim, apply damage
};

// 3. Simple task for victim (being grabbed)
class CTaskSimpleGrabbed : public CTaskSimple {
    CPed* m_pGrabber;
    
    bool ProcessPed(CPed* ped);  // Sync with grabber, play reactions
};
```

### State Flow

```
Player presses GRAB key near ped
        │
        ▼
CTaskComplexGrab assigned to player (PRIMARY slot)
        │
        ├─► CreateFirstSubTask() -> CTaskSimpleGrabInit
        │
        └─► Sends CEventGrabbed to victim
                │
                ▼
        Victim gets CTaskSimpleGrabbed (PHYSICAL_RESPONSE slot)
        
        
While grabbing:
        │
        ├─► ControlSubTask() checks input every frame
        │       │
        │       ├── Attack button -> Switch to CTaskSimpleGrabAction(JAB)
        │       ├── Fwd + Attack  -> Switch to CTaskSimpleGrabAction(THROW)
        │       └── Grab button   -> Switch to CTaskSimpleGrabRelease
        │
        └─► Victim's ProcessPed() syncs position with grabber


Victim escape:
        │
        └─► Random chance based on victim health
                │
                ▼
        CTaskSimpleGrabbed returns true -> triggers grabber release
```

### Key Patterns to Use

1. **ControlSubTask for input** - Check player input every frame
2. **CreateNextSubTask for transitions** - When grab action finishes, return to idle
3. **Secondary task for victim** - Use PHYSICAL_RESPONSE slot (highest priority)
4. **Safe entity refs** - Use SafeRegisterRef for victim pointer
5. **Animation sync** - Match timing between grabber/victim anims

---

## Animation Groups Reference

### fight_a.ifp - Grab Animations

**Grabber (player) animations:**
| Animation | Purpose |
|-----------|---------|
| Fight_grab | Initial grab motion |
| Fight_grab_idle | Looped holding |
| Fight_grab_jab | Jab while holding |
| Fight_grab_stom | Uppercut |
| Fight_grab_fwd | Forward throw |
| Fight_grab_break | Victim escapes reaction |
| Fight_grab_ko_re | Knockout release |

**Victim animations:**
| Animation | Purpose |
|-----------|---------|
| Fight_grabbed | Being grabbed initial |
| Fight_grabbed_idle | Looped being held |
| Fight_grabbed_jab | Jab reaction |
| Fight_grabbed_butt | Uppercut reaction |
| Fight_grabbed_fwd | Being thrown |
| Fight_grabbed_strug | Struggling |
| Fight_grabbed_break | Breaking free |
| Fight_grabbed_ko | Knocked out |

---

*Document generated from gta-reversed source analysis and IDA disassembly*
*Last updated: January 15, 2026*
*Callback crash analysis added after debugging grab system implementation*
