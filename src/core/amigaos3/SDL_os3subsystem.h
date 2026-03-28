/*
  SDL2 -- AmigaOS 3.x Subsystem State Machine
  Tracks lifecycle state of AmigaOS resources (libraries, devices, screens).
  Enforces dependency ordering and provides clean reverse-order shutdown.

  See docs/adr/023-subsystem-state-machine.md for design rationale.

  Usage:
    OS3_SubsystemInit();                      -- call once at startup
    OS3_SetState(OS3_SUBSYS_GRAPHICS, OS3_STATE_INITIALIZING);
    ... open graphics.library ...
    OS3_SetState(OS3_SUBSYS_GRAPHICS, OS3_STATE_READY);
    ...
    OS3_ShutdownAll();                        -- reverse-order teardown
*/

#ifndef SDL_os3subsystem_h_
#define SDL_os3subsystem_h_

#include "../../SDL_internal.h"

/* --- Subsystem identifiers --- */
typedef enum {
    /* Low-level libraries (no dependencies) */
    OS3_SUBSYS_GRAPHICS,      /* graphics.library */
    OS3_SUBSYS_INTUITION,     /* intuition.library */
    OS3_SUBSYS_CYBERGFX,      /* cybergraphics.library / Picasso96API */
    OS3_SUBSYS_TIMER,         /* timer.device */
    OS3_SUBSYS_AHI,           /* ahi.device */

    /* Composite subsystems (have dependencies) */
    OS3_SUBSYS_VIDEO,         /* display mode + window management */
    OS3_SUBSYS_AUDIO,         /* AHI mixing + callbacks */
    OS3_SUBSYS_INPUT,         /* IDCMP event pump */
    OS3_SUBSYS_JOYSTICK,      /* gameport.device */
    OS3_SUBSYS_FILESYSTEM,    /* dos.library paths */

    OS3_SUBSYS_COUNT          /* sentinel -- must be last */
} OS3_SubsystemID;

/* --- Lifecycle states --- */
typedef enum {
    OS3_STATE_NONE,           /* not initialized */
    OS3_STATE_INITIALIZING,   /* init in progress, resources partially open */
    OS3_STATE_READY,          /* fully operational */
    OS3_STATE_ERROR,          /* init failed, cleaned up */
    OS3_STATE_SHUTTING_DOWN   /* shutdown in progress */
} OS3_SubsystemState;

/* --- Shutdown callback --- */
/* Each subsystem registers a shutdown function when it transitions to READY.
   The state machine calls these in reverse init order during ShutdownAll. */
typedef void (*OS3_ShutdownFunc)(void);

/* --- API --- */

/* Initialize the state machine. Call once before any subsystem init.
   Safe to call multiple times (idempotent). */
void OS3_SubsystemInit(void);

/* Query current state of a subsystem. */
OS3_SubsystemState OS3_GetState(OS3_SubsystemID id);

/* Attempt a state transition. Returns 1 on success, 0 on illegal transition.
   When transitioning to READY, pass a shutdown callback (may be NULL).
   For other transitions, shutdown_func is ignored. */
int OS3_SetState(OS3_SubsystemID id, OS3_SubsystemState newstate);

/* Register a shutdown function for a subsystem that is already READY.
   Overwrites any previously registered function. */
void OS3_SetShutdownFunc(OS3_SubsystemID id, OS3_ShutdownFunc func);

/* Check whether all dependencies of `id` are in READY state.
   Returns 1 if all deps are ready (or if subsystem has no deps), 0 otherwise.
   Does NOT check the subsystem's own state. */
int OS3_DepsReady(OS3_SubsystemID id);

/* Shut down all READY subsystems in reverse init order.
   Calls each registered shutdown function, then sets state to NONE.
   Safe to call even if nothing is initialized. */
void OS3_ShutdownAll(void);

/* Reset a subsystem from ERROR to NONE so it can be retried.
   Returns 1 on success, 0 if not in ERROR state. */
int OS3_ResetError(OS3_SubsystemID id);

/* --- Phase gate queries --- */

/* Returns 1 if all prerequisites for the given phase (0-6) are met.
   Phase prerequisites are defined in CLAUDE.md and encoded here.
   Returns 0 if any prerequisite subsystem is not READY. */
int OS3_PhaseReady(int phase);

/* Return human-readable name for a subsystem (for debug logging). */
const char *OS3_SubsystemName(OS3_SubsystemID id);

/* Return human-readable name for a state (for debug logging). */
const char *OS3_StateName(OS3_SubsystemState state);

/* --- Debug --- */

/* Print current state of all subsystems to stdout via printf.
   Intended for test programs and diagnostics. */
void OS3_DumpSubsystems(void);

#endif /* SDL_os3subsystem_h_ */
