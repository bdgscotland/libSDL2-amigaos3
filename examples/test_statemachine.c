/*
 * State Machine & Phase Gate Test
 *
 * Validates the AmigaOS subsystem state machine:
 *   - State transitions (legal and illegal)
 *   - Dependency enforcement
 *   - Reverse-order shutdown
 *   - Phase gate queries
 *   - Integration with SDL_Init(SDL_INIT_VIDEO)
 *
 * Build: make examples
 * Run:   vamos -C 68020 -s 32 -m 8192 examples/test_statemachine
 */

#include <stdio.h>
#include "SDL.h"
#include "../src/core/amigaos3/SDL_os3subsystem.h"

static const char *verstag = "$VER: test_statemachine 0.1 (28.03.2026)";
long __stack = 32768;

static int tests_run = 0;
static int tests_passed = 0;

static void test(const char *name, int condition)
{
    tests_run++;
    if (condition) {
        tests_passed++;
        printf("  PASS: %s\n", name);
    } else {
        printf("  FAIL: %s\n", name);
    }
}

/* Dummy shutdown counter for testing */
static int shutdown_order[OS3_SUBSYS_COUNT];
static int shutdown_count = 0;

static void dummy_shutdown_a(void) { shutdown_order[shutdown_count++] = 0; }
static void dummy_shutdown_b(void) { shutdown_order[shutdown_count++] = 1; }
static void dummy_shutdown_c(void) { shutdown_order[shutdown_count++] = 2; }

int main(int argc, char *argv[])
{
    int rc;

    (void)argc;
    (void)argv;
    (void)verstag;

    printf("SDL2 State Machine Test Suite\n");
    printf("==============================\n\n");

    /* ---- Section 1: Basic state transitions ---- */
    printf("-- Basic Transitions --\n");

    OS3_SubsystemInit();

    test("Initial state is NONE",
         OS3_GetState(OS3_SUBSYS_GRAPHICS) == OS3_STATE_NONE);

    rc = OS3_SetState(OS3_SUBSYS_GRAPHICS, OS3_STATE_INITIALIZING);
    test("NONE -> INITIALIZING succeeds", rc == 1);
    test("State is INITIALIZING",
         OS3_GetState(OS3_SUBSYS_GRAPHICS) == OS3_STATE_INITIALIZING);

    rc = OS3_SetState(OS3_SUBSYS_GRAPHICS, OS3_STATE_READY);
    test("INITIALIZING -> READY succeeds", rc == 1);
    test("State is READY",
         OS3_GetState(OS3_SUBSYS_GRAPHICS) == OS3_STATE_READY);

    /* ---- Section 2: Illegal transitions ---- */
    printf("\n-- Illegal Transitions --\n");

    rc = OS3_SetState(OS3_SUBSYS_GRAPHICS, OS3_STATE_INITIALIZING);
    test("READY -> INITIALIZING blocked", rc == 0);
    test("State unchanged after illegal transition",
         OS3_GetState(OS3_SUBSYS_GRAPHICS) == OS3_STATE_READY);

    /* ---- Section 3: Error and reset ---- */
    printf("\n-- Error and Reset --\n");

    /* Start fresh for timer (no deps) */
    rc = OS3_SetState(OS3_SUBSYS_TIMER, OS3_STATE_INITIALIZING);
    test("Timer NONE -> INITIALIZING", rc == 1);

    rc = OS3_SetState(OS3_SUBSYS_TIMER, OS3_STATE_ERROR);
    test("Timer INITIALIZING -> ERROR", rc == 1);
    test("Timer state is ERROR",
         OS3_GetState(OS3_SUBSYS_TIMER) == OS3_STATE_ERROR);

    rc = OS3_ResetError(OS3_SUBSYS_TIMER);
    test("ResetError succeeds from ERROR", rc == 1);
    test("Timer state is NONE after reset",
         OS3_GetState(OS3_SUBSYS_TIMER) == OS3_STATE_NONE);

    rc = OS3_ResetError(OS3_SUBSYS_TIMER);
    test("ResetError fails from NONE", rc == 0);

    /* ---- Section 4: Dependency enforcement ---- */
    printf("\n-- Dependency Enforcement --\n");

    /* graphics is READY from section 1. intuition depends on graphics. */
    test("Intuition deps ready (graphics=READY)",
         OS3_DepsReady(OS3_SUBSYS_INTUITION) == 1);

    rc = OS3_SetState(OS3_SUBSYS_INTUITION, OS3_STATE_INITIALIZING);
    test("Intuition NONE -> INITIALIZING (deps met)", rc == 1);
    OS3_SetState(OS3_SUBSYS_INTUITION, OS3_STATE_READY);

    /* CyberGfx depends on Intuition */
    test("CyberGfx deps ready (intuition=READY)",
         OS3_DepsReady(OS3_SUBSYS_CYBERGFX) == 1);

    /* AHI depends on Timer -- timer is NONE */
    test("AHI deps NOT ready (timer=NONE)",
         OS3_DepsReady(OS3_SUBSYS_AHI) == 0);

    rc = OS3_SetState(OS3_SUBSYS_AHI, OS3_STATE_INITIALIZING);
    test("AHI INITIALIZING blocked (timer not ready)", rc == 0);

    /* ---- Section 5: Reverse-order shutdown ---- */
    printf("\n-- Reverse-Order Shutdown --\n");

    /* Reset everything for a clean test */
    OS3_ShutdownAll();
    shutdown_count = 0;

    /* Init 3 independent subsystems in specific order */
    OS3_SetState(OS3_SUBSYS_GRAPHICS, OS3_STATE_INITIALIZING);
    OS3_SetState(OS3_SUBSYS_GRAPHICS, OS3_STATE_READY);
    OS3_SetShutdownFunc(OS3_SUBSYS_GRAPHICS, dummy_shutdown_a);

    OS3_SetState(OS3_SUBSYS_INTUITION, OS3_STATE_INITIALIZING);
    OS3_SetState(OS3_SUBSYS_INTUITION, OS3_STATE_READY);
    OS3_SetShutdownFunc(OS3_SUBSYS_INTUITION, dummy_shutdown_b);

    OS3_SetState(OS3_SUBSYS_CYBERGFX, OS3_STATE_INITIALIZING);
    OS3_SetState(OS3_SUBSYS_CYBERGFX, OS3_STATE_READY);
    OS3_SetShutdownFunc(OS3_SUBSYS_CYBERGFX, dummy_shutdown_c);

    OS3_ShutdownAll();

    test("3 shutdown callbacks called", shutdown_count == 3);
    test("CyberGfx shut down first (reverse order)",
         shutdown_count >= 1 && shutdown_order[0] == 2);
    test("Intuition shut down second",
         shutdown_count >= 2 && shutdown_order[1] == 1);
    test("Graphics shut down last",
         shutdown_count >= 3 && shutdown_order[2] == 0);

    test("All states NONE after shutdown",
         OS3_GetState(OS3_SUBSYS_GRAPHICS) == OS3_STATE_NONE &&
         OS3_GetState(OS3_SUBSYS_INTUITION) == OS3_STATE_NONE &&
         OS3_GetState(OS3_SUBSYS_CYBERGFX) == OS3_STATE_NONE);

    /* ---- Section 6: Phase gate queries ---- */
    printf("\n-- Phase Gates --\n");

    test("Phase 0 not ready (nothing init'd)", OS3_PhaseReady(0) == 0);

    OS3_SetState(OS3_SUBSYS_GRAPHICS, OS3_STATE_INITIALIZING);
    OS3_SetState(OS3_SUBSYS_GRAPHICS, OS3_STATE_READY);
    test("Phase 0 ready (graphics=READY)", OS3_PhaseReady(0) == 1);
    test("Phase 1 not ready (video not init'd)", OS3_PhaseReady(1) == 0);

    /* ---- Section 7: Name helpers ---- */
    printf("\n-- Debug Helpers --\n");

    test("SubsystemName(GRAPHICS) correct",
         SDL_strcmp(OS3_SubsystemName(OS3_SUBSYS_GRAPHICS),
                    "graphics.library") == 0);
    test("StateName(READY) correct",
         SDL_strcmp(OS3_StateName(OS3_STATE_READY), "READY") == 0);

    /* ---- Section 8: Integration with SDL_Init ---- */
    printf("\n-- SDL_Init Integration --\n");

    OS3_ShutdownAll();  /* clean slate */

    rc = SDL_Init(SDL_INIT_VIDEO);
    if (rc == 0) {
        test("SDL_Init(VIDEO) succeeded", 1);
        test("Phase 0 ready after video init", OS3_PhaseReady(0) == 1);
        test("Phase 1 ready after video init", OS3_PhaseReady(1) == 1);
        printf("\n");
        OS3_DumpSubsystems();
        SDL_Quit();
        test("All subsystems NONE after SDL_Quit",
             OS3_GetState(OS3_SUBSYS_GRAPHICS) == OS3_STATE_NONE);
    } else {
        test("SDL_Init(VIDEO) returned error (expected on vamos)", 1);
        printf("    (%s)\n", SDL_GetError());
    }

    /* ---- Summary ---- */
    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 10;
}
