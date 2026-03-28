/*
  SDL2 Video Driver -- AmigaOS 3.x (CyberGraphX)
  Phase 1: Real CyberGraphX RTG backend with software renderer.

  Library open/close order (CRITICAL -- see cybergraphx-reference.md #9):
    Open:  graphics -> intuition -> cybergraphics
    Close: cybergraphics -> intuition -> graphics
    Window must be closed before screen. Screen before libraries.
*/

#include "../../SDL_internal.h"

#if SDL_VIDEO_DRIVER_AMIGAOS3

#include <stdio.h>
#include <proto/dos.h>
#include "SDL_os3video.h"
#include "SDL_os3window.h"
#include "SDL_os3framebuffer.h"
#include "SDL_os3events.h"
#include "../SDL_sysvideo.h"
#include "../../events/SDL_events_c.h"
#include "../../core/amigaos3/SDL_os3subsystem.h"

/*
 * AmigaOS library bases.
 * IntuitionBase is struct IntuitionBase * (declared by proto/intuition.h).
 * GfxBase is struct GfxBase * (declared by proto/graphics.h).
 * CyberGfxBase is struct Library * (declared by proto/cybergraphics.h).
 * All are defined as NULL here and opened in OS3_VideoInit.
 */
struct Library         *CyberGfxBase  = NULL;
struct IntuitionBase   *IntuitionBase = NULL;
struct GfxBase         *GfxBase       = NULL;

#define OS3VID_DRIVER_NAME "amigaos3"

/* Forward declarations */
static int  OS3_VideoInit(_THIS);
static void OS3_VideoQuit(_THIS);
static void OS3_DeleteDevice(SDL_VideoDevice *device);
static void OS3_GetDisplayModes(_THIS, SDL_VideoDisplay *display);
static int  OS3_SetDisplayMode(_THIS, SDL_VideoDisplay *display,
                               SDL_DisplayMode *mode);

/* Map CyberGraphX depth to SDL pixel format.
   Returns SDL_PIXELFORMAT_UNKNOWN for unsupported depths. */
static Uint32 OS3_DepthToFormat(ULONG depth)
{
    switch (depth) {
    case 15: return SDL_PIXELFORMAT_RGB555;
    case 16: return SDL_PIXELFORMAT_RGB565;
    case 24: return SDL_PIXELFORMAT_RGB24;
    case 32: return SDL_PIXELFORMAT_ARGB8888;
    default: return SDL_PIXELFORMAT_UNKNOWN;
    }
}

static SDL_VideoDevice *OS3_CreateDevice(void)
{
    SDL_VideoDevice *device;

    device = (SDL_VideoDevice *)SDL_calloc(1, sizeof(SDL_VideoDevice));
    if (device == NULL) {
        SDL_OutOfMemory();
        return NULL;
    }

    /* Initialization */
    device->VideoInit    = OS3_VideoInit;
    device->VideoQuit    = OS3_VideoQuit;
    device->free         = OS3_DeleteDevice;

    /* Display */
    device->GetDisplayModes = OS3_GetDisplayModes;
    device->SetDisplayMode  = OS3_SetDisplayMode;

    /* Window */
    device->CreateSDLWindow  = OS3_CreateWindow;
    device->DestroyWindow    = OS3_DestroyWindow;
    device->SetWindowTitle   = OS3_SetWindowTitle;
    device->ShowWindow       = OS3_ShowWindow;
    device->HideWindow       = OS3_HideWindow;
    device->RaiseWindow      = OS3_RaiseWindow;

    /* Framebuffer (software renderer path) */
    device->CreateWindowFramebuffer  = OS3_CreateWindowFramebuffer;
    device->UpdateWindowFramebuffer  = OS3_UpdateWindowFramebuffer;
    device->DestroyWindowFramebuffer = OS3_DestroyWindowFramebuffer;

    /* Events */
    device->PumpEvents = OS3_PumpEvents;

    return device;
}

VideoBootStrap OS3_bootstrap = {
    OS3VID_DRIVER_NAME, "AmigaOS 3.x CyberGraphX/P96",
    OS3_CreateDevice, NULL /* ShowMessageBox */
};

/* Shutdown callbacks for the state machine */
static void OS3_ShutdownGraphics(void)
{
    if (GfxBase) {
        CloseLibrary((struct Library *)GfxBase);
        GfxBase = NULL;
    }
}

static void OS3_ShutdownIntuition(void)
{
    if (IntuitionBase) {
        CloseLibrary((struct Library *)IntuitionBase);
        IntuitionBase = NULL;
    }
}

static void OS3_ShutdownCyberGfx(void)
{
    if (CyberGfxBase) {
        CloseLibrary(CyberGfxBase);
        CyberGfxBase = NULL;
    }
}

static int OS3_VideoInit(_THIS)
{
    ULONG nextid;
    SDL_DisplayMode best_mode;
    int found_any;
    struct Process *me;
    APTR oldwin;

    OS3_SubsystemInit();

    /* Suppress AmigaDOS system requesters ("Please insert volume...")
       during library opens. Without this, a missing cybergraphics.library
       causes an invisible requester that hangs the program.
       See known-pitfalls.md: "AmigaDOS Volume Requester on Path Probing" */
    me = (struct Process *)FindTask(NULL);
    oldwin = me->pr_WindowPtr;
    me->pr_WindowPtr = (APTR)-1L;

    /* --- graphics.library --- */
    OS3_SetState(OS3_SUBSYS_GRAPHICS, OS3_STATE_INITIALIZING);
    printf("[OS3_VideoInit] opening graphics.library...\n");
    fflush(stdout);

    GfxBase = (struct GfxBase *)
              OpenLibrary((CONST_STRPTR)"graphics.library", 39UL);
    if (!GfxBase) {
        OS3_SetState(OS3_SUBSYS_GRAPHICS, OS3_STATE_ERROR);
        me->pr_WindowPtr = oldwin;
        return SDL_SetError("Cannot open graphics.library V39+");
    }
    OS3_SetState(OS3_SUBSYS_GRAPHICS, OS3_STATE_READY);
    OS3_SetShutdownFunc(OS3_SUBSYS_GRAPHICS, OS3_ShutdownGraphics);

    /* --- intuition.library --- */
    OS3_SetState(OS3_SUBSYS_INTUITION, OS3_STATE_INITIALIZING);
    printf("[OS3_VideoInit] opening intuition.library...\n");
    fflush(stdout);

    IntuitionBase = (struct IntuitionBase *)
                    OpenLibrary((CONST_STRPTR)"intuition.library", 39UL);
    if (!IntuitionBase) {
        OS3_SetState(OS3_SUBSYS_INTUITION, OS3_STATE_ERROR);
        me->pr_WindowPtr = oldwin;
        OS3_ShutdownAll();
        return SDL_SetError("Cannot open intuition.library V39+");
    }
    OS3_SetState(OS3_SUBSYS_INTUITION, OS3_STATE_READY);
    OS3_SetShutdownFunc(OS3_SUBSYS_INTUITION, OS3_ShutdownIntuition);

    /* --- cybergraphics.library --- */
    OS3_SetState(OS3_SUBSYS_CYBERGFX, OS3_STATE_INITIALIZING);
    printf("[OS3_VideoInit] opening cybergraphics.library...\n");
    fflush(stdout);

    /* Try CyberGraphX name first (native CGX or P96 with monitor driver).
       Fall back to Picasso96API.library (P96 without monitor driver,
       common on FS-UAE where uaegfx provides the card but the P96
       monitor driver is not installed). Both provide the same API. */
    CyberGfxBase = OpenLibrary((CONST_STRPTR)CYBERGFXNAME, 40UL);
    if (!CyberGfxBase) {
        CyberGfxBase = OpenLibrary(
            (CONST_STRPTR)"Picasso96API.library", 0UL);
    }

    /* Restore requesters now that library opens are done */
    me->pr_WindowPtr = oldwin;

    printf("[OS3_VideoInit] cybergraphics.library: %s\n",
           CyberGfxBase ? "OK" : "FAILED");
    fflush(stdout);

    if (!CyberGfxBase) {
        OS3_SetState(OS3_SUBSYS_CYBERGFX, OS3_STATE_ERROR);
        OS3_ShutdownAll();
        return SDL_SetError("Cannot open cybergraphics.library V40+"
                            " -- no RTG board or driver not installed");
    }
    OS3_SetState(OS3_SUBSYS_CYBERGFX, OS3_STATE_READY);
    OS3_SetShutdownFunc(OS3_SUBSYS_CYBERGFX, OS3_ShutdownCyberGfx);

    /*
     * Enumerate RTG display modes to build the SDL display list.
     *
     * Strategy: collect all CGX modes, find the largest as the
     * "desktop mode", then call SDL_AddBasicVideoDisplay to register
     * the display. SDL_AddDisplayMode adds additional modes later
     * via GetDisplayModes.
     *
     * We only look for the best mode here. GetDisplayModes fills in
     * the full list.
     */
    OS3_SetState(OS3_SUBSYS_VIDEO, OS3_STATE_INITIALIZING);

    SDL_zero(best_mode);
    best_mode.format = SDL_PIXELFORMAT_ARGB8888;
    best_mode.w = 640;
    best_mode.h = 480;
    best_mode.refresh_rate = 60;
    best_mode.driverdata = NULL;

    found_any = 0;
    nextid = NextDisplayInfo(INVALID_ID);
    while (nextid != INVALID_ID) {
        if (IsCyberModeID(nextid)) {
            ULONG w   = GetCyberIDAttr(CYBRIDATTR_WIDTH,  nextid);
            ULONG h   = GetCyberIDAttr(CYBRIDATTR_HEIGHT, nextid);
            ULONG bpp = GetCyberIDAttr(CYBRIDATTR_DEPTH,  nextid);
            Uint32 fmt = OS3_DepthToFormat((int)bpp);

            if (fmt != SDL_PIXELFORMAT_UNKNOWN) {
                if (!found_any || (int)w > best_mode.w ||
                    ((int)w == best_mode.w && (int)h > best_mode.h)) {
                    best_mode.w      = (int)w;
                    best_mode.h      = (int)h;
                    best_mode.format = fmt;
                    found_any = 1;
                }
            }
        }
        nextid = NextDisplayInfo(nextid);
    }

    if (SDL_AddBasicVideoDisplay(&best_mode) < 0) {
        OS3_SetState(OS3_SUBSYS_VIDEO, OS3_STATE_ERROR);
        OS3_ShutdownAll();
        return -1;
    }

    OS3_SetState(OS3_SUBSYS_VIDEO, OS3_STATE_READY);
    /* VIDEO shutdown is handled by OS3_VideoQuit, registered below */

    printf("[OS3_VideoInit] subsystem state:\n");
    OS3_DumpSubsystems();

    return 0;
}

static void OS3_VideoQuit(_THIS)
{
    /* Mark VIDEO composite as shutting down, then let the state machine
       tear down libraries in reverse init order (cgx -> intuition -> gfx).
       Windows and screens must be closed already (DestroyWindow is called
       before VideoQuit). */
    if (OS3_GetState(OS3_SUBSYS_VIDEO) == OS3_STATE_READY) {
        OS3_SetState(OS3_SUBSYS_VIDEO, OS3_STATE_SHUTTING_DOWN);
        OS3_SetState(OS3_SUBSYS_VIDEO, OS3_STATE_NONE);
    }
    OS3_ShutdownAll();
}

static void OS3_GetDisplayModes(_THIS, SDL_VideoDisplay *display)
{
    ULONG nextid;

    nextid = NextDisplayInfo(INVALID_ID);
    while (nextid != INVALID_ID) {
        if (IsCyberModeID(nextid)) {
            ULONG w   = GetCyberIDAttr(CYBRIDATTR_WIDTH,  nextid);
            ULONG h   = GetCyberIDAttr(CYBRIDATTR_HEIGHT, nextid);
            ULONG bpp = GetCyberIDAttr(CYBRIDATTR_DEPTH,  nextid);
            Uint32 fmt = OS3_DepthToFormat((int)bpp);

            if (fmt != SDL_PIXELFORMAT_UNKNOWN) {
                SDL_DisplayMode mode;
                SDL_zero(mode);
                mode.format       = fmt;
                mode.w            = (int)w;
                mode.h            = (int)h;
                mode.refresh_rate = 60;
                mode.driverdata   = NULL;
                SDL_AddDisplayMode(display, &mode);
            }
        }
        nextid = NextDisplayInfo(nextid);
    }
}

static int OS3_SetDisplayMode(_THIS, SDL_VideoDisplay *display,
                              SDL_DisplayMode *mode)
{
    /* Phase 1: stub -- windowed mode does not switch display modes.
       Full display switching deferred to Phase 5. */
    (void)display;
    (void)mode;
    return 0;
}

static void OS3_DeleteDevice(SDL_VideoDevice *device)
{
    SDL_free(device);
}

#endif /* SDL_VIDEO_DRIVER_AMIGAOS3 */
