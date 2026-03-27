/*
 * SDL2 Dynamic Loading -- Stub (AmigaOS 3.x has no dlopen)
 *
 * All functions return failure. SDL_DYNAMIC_API is disabled.
 */

void *OS3_LoadObject(const char *sofile)
{
    (void)sofile;
    return 0;  /* not supported */
}

void *OS3_LoadFunction(void *handle, const char *name)
{
    (void)handle;
    (void)name;
    return 0;  /* not supported */
}

void OS3_UnloadObject(void *handle)
{
    (void)handle;
}
