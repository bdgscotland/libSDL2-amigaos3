# ADR-023: Subsystem State Machine for AmigaOS Resource Lifecycle

**Date:** 2026-03-28
**Status:** Accepted
**Deciders:** Project leads

## Context

AmigaOS 3.x has no memory protection and no automatic resource cleanup. Every
`OpenLibrary()` must have a `CloseLibrary()`, every `OpenDevice()` a
`CloseDevice()`, every `OpenScreen()` a `CloseScreen()`, and these must happen
in strict reverse order. A missed cleanup leaks resources until reboot.

The current codebase manages this with hand-coded error cascades in each
backend's init function. `OS3_VideoInit()` opens graphics.library, then
intuition.library, then cybergraphics.library -- and each failure path must
close everything opened so far. This pattern:

1. **Duplicates cleanup logic** at every error return point
2. **Cannot be queried** -- no way to ask "is the video subsystem fully
   initialized?" vs "partially initialized due to error"
3. **Has no dependency enforcement** -- nothing prevents audio init from
   proceeding when timer (needed for AHI callbacks) is not ready
4. **Cannot orchestrate shutdown** -- SDL_Quit must tear down subsystems in
   the right order, but the order is implicit in code flow

Additionally, the project uses a 6-phase development model (Bootstrap through
Optimization) with explicit gate prerequisites. These gates are currently
enforced by human review of CLAUDE.md -- nothing programmatic validates that
Phase N-1 is complete before Phase N work begins.

## Decision

Implement a lightweight subsystem state machine that:

1. **Tracks lifecycle state** for each AmigaOS backend subsystem
2. **Validates transitions** -- only legal state changes are allowed
3. **Encodes dependencies** -- subsystem B cannot init until subsystem A is ready
4. **Provides ordered shutdown** -- `OS3_ShutdownAll()` tears down in reverse
   dependency order
5. **Exposes phase readiness** -- `OS3_PhaseReady(n)` returns whether all
   prerequisites for phase N are met

### State Model

Each subsystem progresses through:

```
  NONE --> INITIALIZING --> READY --> SHUTTING_DOWN --> NONE
                |                          ^
                v                          |
              ERROR -------(reset)-------->+
```

- **NONE**: Not initialized. Resources not allocated.
- **INITIALIZING**: Init in progress. Some resources may be partially allocated.
- **READY**: Fully initialized and operational.
- **ERROR**: Init failed. Partial resources cleaned up by the subsystem itself.
- **SHUTTING_DOWN**: Shutdown in progress.

### Subsystem Registry

```
OS3_SUBSYS_GRAPHICS     -- graphics.library
OS3_SUBSYS_INTUITION    -- intuition.library
OS3_SUBSYS_CYBERGFX     -- cybergraphics.library
OS3_SUBSYS_TIMER        -- timer.device
OS3_SUBSYS_AHI          -- ahi.device
OS3_SUBSYS_VIDEO        -- composite: graphics + intuition + cybergfx + display
OS3_SUBSYS_AUDIO        -- composite: ahi + mixing
OS3_SUBSYS_INPUT        -- IDCMP event pump (depends on video)
OS3_SUBSYS_JOYSTICK     -- gameport.device
OS3_SUBSYS_FILESYSTEM   -- dos.library paths
```

### Dependency Graph

```
VIDEO --> CYBERGFX --> INTUITION --> GRAPHICS
AUDIO --> AHI --> TIMER
INPUT --> VIDEO
JOYSTICK (independent)
FILESYSTEM (independent)
```

"A --> B" means A depends on B. B must be READY before A can transition to
INITIALIZING.

### Phase Gate Mapping

```
Phase 0: OS3_SUBSYS_GRAPHICS=READY (libraries open, SDL_Init(0) works)
Phase 1: OS3_SUBSYS_VIDEO=READY (window opens, pixels draw)
Phase 2: OS3_SUBSYS_INPUT=READY (IDCMP events flow)
Phase 3: OS3_SUBSYS_AUDIO=READY (AHI plays sound)
Phase 4: (threading -- already working, no OS3 subsystem needed)
Phase 5: OS3_SUBSYS_TIMER=READY, OS3_SUBSYS_FILESYSTEM=READY,
         OS3_SUBSYS_JOYSTICK=READY
```

## Consequences

### Positive

- **Single source of truth** for resource state -- no more guessing whether
  cybergraphics.library is open
- **Automatic reverse-order shutdown** -- register init order, shutdown
  reverses it
- **Dependency violations caught early** -- trying to init audio before timer
  is a clear error message, not a mysterious crash
- **Phase gates become testable** -- `OS3_PhaseReady(1)` can be checked in
  automated tests
- **Eliminates duplicated cleanup** -- each subsystem's init registers its
  cleanup; the state machine calls it

### Negative

- **Adds ~200 lines of C** to the library
- **Slight indirection** -- backends call through state machine instead of
  directly opening libraries
- **Must be maintained** as new subsystems are added

### Constraints

- Zero dynamic allocation in the state machine itself (static arrays only)
- No AmigaOS API calls in the state machine core -- it is pure logic
- Stack usage < 64 bytes (it is just array indexing)
- ASCII only in source (per project rules)

## Implementation

- `src/core/amigaos3/SDL_os3subsystem.h` -- enums, API declarations
- `src/core/amigaos3/SDL_os3subsystem.c` -- state table, transition logic,
  dependency graph, shutdown orchestration
- Backends call `OS3_SubsystemTransition(id, newstate)` instead of managing
  state implicitly
- `OS3_ShutdownAll()` called from `SDL_Quit()` path as a safety net
