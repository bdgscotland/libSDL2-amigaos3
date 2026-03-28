/*
  SDL2 -- AmigaOS 3.x Subsystem State Machine
  See SDL_os3subsystem.h and docs/adr/023-subsystem-state-machine.md.

  Pure logic -- no AmigaOS API calls, no dynamic allocation.
  Stack usage: ~32 bytes (array indices and locals only).
*/

#include "SDL_os3subsystem.h"
#include <stdio.h>

/* ------------------------------------------------------------------ */
/* State table                                                         */
/* ------------------------------------------------------------------ */

static OS3_SubsystemState state_table[OS3_SUBSYS_COUNT];
static OS3_ShutdownFunc   shutdown_table[OS3_SUBSYS_COUNT];

/* Init order tracking -- records the order subsystems reached READY,
   so ShutdownAll can reverse it. */
#define MAX_INIT_ORDER OS3_SUBSYS_COUNT
static OS3_SubsystemID init_order[MAX_INIT_ORDER];
static int              init_count = 0;

static int initialized = 0;

/* ------------------------------------------------------------------ */
/* Dependency graph                                                    */
/* ------------------------------------------------------------------ */

/* Each subsystem has a list of dependencies (terminated by -1).
   A dependency must be READY before this subsystem can enter INITIALIZING. */

#define DEP_END (-1)

/* Max 4 deps per subsystem is plenty for our architecture */
static const int dependency_graph[OS3_SUBSYS_COUNT][5] = {
    /* OS3_SUBSYS_GRAPHICS    */ { DEP_END, 0, 0, 0, 0 },
    /* OS3_SUBSYS_INTUITION   */ { OS3_SUBSYS_GRAPHICS, DEP_END, 0, 0, 0 },
    /* OS3_SUBSYS_CYBERGFX    */ { OS3_SUBSYS_INTUITION, DEP_END, 0, 0, 0 },
    /* OS3_SUBSYS_TIMER       */ { DEP_END, 0, 0, 0, 0 },
    /* OS3_SUBSYS_AHI         */ { OS3_SUBSYS_TIMER, DEP_END, 0, 0, 0 },

    /* OS3_SUBSYS_VIDEO       */ { OS3_SUBSYS_CYBERGFX, DEP_END, 0, 0, 0 },
    /* OS3_SUBSYS_AUDIO       */ { OS3_SUBSYS_AHI, DEP_END, 0, 0, 0 },
    /* OS3_SUBSYS_INPUT       */ { OS3_SUBSYS_VIDEO, DEP_END, 0, 0, 0 },
    /* OS3_SUBSYS_JOYSTICK    */ { DEP_END, 0, 0, 0, 0 },
    /* OS3_SUBSYS_FILESYSTEM  */ { DEP_END, 0, 0, 0, 0 },
};

/* ------------------------------------------------------------------ */
/* Transition validation                                               */
/* ------------------------------------------------------------------ */

/*
 * Legal transitions:
 *   NONE          -> INITIALIZING
 *   INITIALIZING  -> READY
 *   INITIALIZING  -> ERROR
 *   READY         -> SHUTTING_DOWN
 *   SHUTTING_DOWN  -> NONE
 *   ERROR         -> NONE  (via OS3_ResetError)
 */
static int is_valid_transition(OS3_SubsystemState from, OS3_SubsystemState to)
{
    if (from == OS3_STATE_NONE && to == OS3_STATE_INITIALIZING)
        return 1;
    if (from == OS3_STATE_INITIALIZING && to == OS3_STATE_READY)
        return 1;
    if (from == OS3_STATE_INITIALIZING && to == OS3_STATE_ERROR)
        return 1;
    if (from == OS3_STATE_READY && to == OS3_STATE_SHUTTING_DOWN)
        return 1;
    if (from == OS3_STATE_SHUTTING_DOWN && to == OS3_STATE_NONE)
        return 1;
    if (from == OS3_STATE_ERROR && to == OS3_STATE_NONE)
        return 1;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Name tables (for debug output)                                      */
/* ------------------------------------------------------------------ */

static const char *subsystem_names[OS3_SUBSYS_COUNT] = {
    "graphics.library",
    "intuition.library",
    "cybergraphics.library",
    "timer.device",
    "ahi.device",
    "video",
    "audio",
    "input",
    "joystick",
    "filesystem"
};

static const char *state_names[] = {
    "NONE",
    "INITIALIZING",
    "READY",
    "ERROR",
    "SHUTTING_DOWN"
};

/* ------------------------------------------------------------------ */
/* Implementation                                                      */
/* ------------------------------------------------------------------ */

void OS3_SubsystemInit(void)
{
    int i;
    if (initialized) {
        return;
    }
    for (i = 0; i < OS3_SUBSYS_COUNT; i++) {
        state_table[i] = OS3_STATE_NONE;
        shutdown_table[i] = NULL;
    }
    init_count = 0;
    initialized = 1;
}

OS3_SubsystemState OS3_GetState(OS3_SubsystemID id)
{
    if (id < 0 || id >= OS3_SUBSYS_COUNT) {
        return OS3_STATE_NONE;
    }
    return state_table[id];
}

int OS3_DepsReady(OS3_SubsystemID id)
{
    int i;
    if (id < 0 || id >= OS3_SUBSYS_COUNT) {
        return 0;
    }
    for (i = 0; i < 5; i++) {
        int dep = dependency_graph[id][i];
        if (dep == DEP_END) {
            break;
        }
        if (dep < 0 || dep >= OS3_SUBSYS_COUNT) {
            break;
        }
        if (state_table[dep] != OS3_STATE_READY) {
            return 0;
        }
    }
    return 1;
}

int OS3_SetState(OS3_SubsystemID id, OS3_SubsystemState newstate)
{
    OS3_SubsystemState oldstate;

    if (!initialized) {
        OS3_SubsystemInit();
    }

    if (id < 0 || id >= OS3_SUBSYS_COUNT) {
        return 0;
    }

    oldstate = state_table[id];

    if (!is_valid_transition(oldstate, newstate)) {
        printf("[OS3_SM] ILLEGAL transition: %s %s -> %s\n",
               subsystem_names[id],
               state_names[oldstate],
               state_names[newstate]);
        return 0;
    }

    /* Check dependencies when entering INITIALIZING */
    if (newstate == OS3_STATE_INITIALIZING) {
        if (!OS3_DepsReady(id)) {
            printf("[OS3_SM] BLOCKED: %s deps not ready\n",
                   subsystem_names[id]);
            return 0;
        }
    }

    /* Record init order when reaching READY */
    if (newstate == OS3_STATE_READY) {
        if (init_count < MAX_INIT_ORDER) {
            init_order[init_count++] = id;
        }
    }

    state_table[id] = newstate;
    return 1;
}

void OS3_SetShutdownFunc(OS3_SubsystemID id, OS3_ShutdownFunc func)
{
    if (id >= 0 && id < OS3_SUBSYS_COUNT) {
        shutdown_table[id] = func;
    }
}

void OS3_ShutdownAll(void)
{
    int i;
    OS3_SubsystemID id;

    if (!initialized) {
        return;
    }

    /* Shut down in reverse init order */
    for (i = init_count - 1; i >= 0; i--) {
        id = init_order[i];
        if (state_table[id] == OS3_STATE_READY) {
            state_table[id] = OS3_STATE_SHUTTING_DOWN;

            if (shutdown_table[id]) {
                shutdown_table[id]();
            }

            state_table[id] = OS3_STATE_NONE;
            shutdown_table[id] = NULL;
        }
    }

    init_count = 0;
}

int OS3_ResetError(OS3_SubsystemID id)
{
    if (id < 0 || id >= OS3_SUBSYS_COUNT) {
        return 0;
    }
    if (state_table[id] != OS3_STATE_ERROR) {
        return 0;
    }
    state_table[id] = OS3_STATE_NONE;
    shutdown_table[id] = NULL;
    return 1;
}

/* ------------------------------------------------------------------ */
/* Phase gate queries                                                  */
/* ------------------------------------------------------------------ */

/*
 * Phase prerequisites (from CLAUDE.md):
 *   Phase 0: graphics.library opens (bootstrap)
 *   Phase 1: VIDEO ready (window + pixels)
 *   Phase 2: INPUT ready (IDCMP events)
 *   Phase 3: AUDIO ready (AHI)
 *   Phase 4: (threading -- no OS3 subsystem, always ready)
 *   Phase 5: TIMER + FILESYSTEM + JOYSTICK ready
 *   Phase 6: all of Phase 5 (optimization only)
 */
int OS3_PhaseReady(int phase)
{
    switch (phase) {
    case 0:
        return (state_table[OS3_SUBSYS_GRAPHICS] == OS3_STATE_READY);
    case 1:
        return (state_table[OS3_SUBSYS_VIDEO] == OS3_STATE_READY);
    case 2:
        return (state_table[OS3_SUBSYS_INPUT] == OS3_STATE_READY);
    case 3:
        return (state_table[OS3_SUBSYS_AUDIO] == OS3_STATE_READY);
    case 4:
        /* Threading is done via Exec Tasks, not tracked here.
           Phase 4 gate = Phase 3 complete. */
        return OS3_PhaseReady(3);
    case 5:
        return (state_table[OS3_SUBSYS_TIMER] == OS3_STATE_READY &&
                state_table[OS3_SUBSYS_FILESYSTEM] == OS3_STATE_READY &&
                state_table[OS3_SUBSYS_JOYSTICK] == OS3_STATE_READY);
    case 6:
        return OS3_PhaseReady(5);
    default:
        return 0;
    }
}

/* ------------------------------------------------------------------ */
/* Debug helpers                                                       */
/* ------------------------------------------------------------------ */

const char *OS3_SubsystemName(OS3_SubsystemID id)
{
    if (id >= 0 && id < OS3_SUBSYS_COUNT) {
        return subsystem_names[id];
    }
    return "unknown";
}

const char *OS3_StateName(OS3_SubsystemState state)
{
    if (state >= OS3_STATE_NONE && state <= OS3_STATE_SHUTTING_DOWN) {
        return state_names[state];
    }
    return "invalid";
}

void OS3_DumpSubsystems(void)
{
    int i;
    int phase;

    printf("=== AmigaOS3 Subsystem State ===\n");
    for (i = 0; i < OS3_SUBSYS_COUNT; i++) {
        printf("  %-24s %s", subsystem_names[i], state_names[state_table[i]]);
        if (state_table[i] == OS3_STATE_NONE && !OS3_DepsReady(i)) {
            printf("  (deps not ready)");
        }
        printf("\n");
    }

    printf("--- Phase Gates ---\n");
    for (phase = 0; phase <= 6; phase++) {
        printf("  Phase %d: %s\n", phase,
               OS3_PhaseReady(phase) ? "READY" : "not ready");
    }
    printf("================================\n");
}
