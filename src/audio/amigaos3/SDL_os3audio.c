/*
  SDL2 Audio Driver -- AmigaOS 3.x (AHI)
  Phase 0: Stub -- registers bootstrap, init returns SDL_FALSE.
  Phase 3: Real AHI audio backend.
*/

#include "../../SDL_internal.h"

#if SDL_AUDIO_DRIVER_AHI

#include "SDL_audio.h"
#include "../SDL_audio_c.h"
#include "../SDL_sysaudio.h"
#include "../../core/amigaos3/SDL_os3subsystem.h"

#define OS3AHI_DRIVER_NAME "ahi"

static SDL_bool OS3AHI_Init(SDL_AudioDriverImpl *impl)
{
    /* Phase 0: report deps not ready and return SDL_FALSE.
       Phase 3: Open ahi.device, fill in impl callbacks. */
    if (!OS3_DepsReady(OS3_SUBSYS_AUDIO)) {
        /* Timer subsystem not ready -- AHI callbacks need timing */
        return SDL_FALSE;
    }
    /* Phase 3 implementation goes here */
    return SDL_FALSE;
}

AudioBootStrap OS3AHI_bootstrap = {
    OS3AHI_DRIVER_NAME, "AmigaOS AHI Audio",
    OS3AHI_Init, SDL_FALSE /* not demand_only */
};

#endif /* SDL_AUDIO_DRIVER_AHI */
