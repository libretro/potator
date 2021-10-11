#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <stdio.h>
#include <stdlib.h>

#include <SDL.h>

#include "../../common/supervision.h"

#define VERSION "1.0.1"

#define OR_DIE(cond) \
    if (cond) { \
        fprintf(stderr, "[Error] SDL: %s\n", SDL_GetError()); \
        exit(1); \
    }

#define UPDATE_RATE 60

SDL_bool done = SDL_FALSE;

uint8_t *buffer;
uint32_t bufferSize = 0;

uint16_t screenBuffer[SV_W * SV_H];
SDL_Surface *PrimarySurface;
int windowScale = 4;
int windowWidth = 0;
int windowHeight = 0;
int maxWindowWidth = 0;
int maxWindowHeight = 0;
SDL_bool isFullscreen = SDL_FALSE;
void ToggleFullscreen(void);
void NextPalette(void);
void UpdateWindowSize(void);
void IncreaseWindowSize(void);
void DecreaseWindowSize(void);
int currentGhosting = 0;
void IncreaseGhosting(void);
void DecreaseGhosting(void);

char romName[64];
void SetRomName(const char *path)
{
    const char *p = path + strlen(path);
    while (p != path) {
        if (*p == '\\' || *p == '/') {
            p++;
            break;
        }
        p--;
    }
    strncpy(romName, p, sizeof(romName));
    romName[sizeof(romName) - 1] = '\0';
}

int LoadROM(const char *filename)
{
    if (buffer != NULL) {
        free(buffer);
        buffer = NULL;
    }

    SDL_RWops *romfile = SDL_RWFromFile(filename, "rb");
    if (romfile == NULL) {
        fprintf(stderr, "SDL_RWFromFile(): Unable to open file! (%s)\n", filename);
        return 1;
    }
    SDL_RWseek(romfile, 0, RW_SEEK_END);
    bufferSize = (uint32_t)SDL_RWtell(romfile);
    SDL_RWseek(romfile, 0, RW_SEEK_SET);
    buffer = (uint8_t *)malloc(bufferSize);
    SDL_RWread(romfile, buffer, bufferSize, 1);
    if (SDL_RWclose(romfile) != 0) {
        fprintf(stderr, "SDL_RWclose(): Unable to close file! (%s)\n", filename);
        return 1;
    }
    SetRomName(filename);
    return 0;
}

void PollEvents(void)
{
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) {
            done = SDL_TRUE;
        }
        else if (event.type == SDL_KEYDOWN) {
            switch (event.key.keysym.sym) {
            case SDLK_RETURN:
                ToggleFullscreen();
                break;
            case SDLK_TAB:
                supervision_load(buffer, bufferSize);
                break;
            case SDLK_1:
                supervision_save_state(romName, 0);
                break;
            case SDLK_2:
                supervision_load_state(romName, 0);
                break;
            case SDLK_p:
                NextPalette();
                break;
            case SDLK_MINUS:
                DecreaseWindowSize();
                break;
            case SDLK_EQUALS:
                IncreaseWindowSize();
                break;
            case SDLK_LEFTBRACKET:
                DecreaseGhosting();
                break;
            case SDLK_RIGHTBRACKET:
                IncreaseGhosting();
                break;
            case SDLK_m:
                SDL_PauseAudio(SDL_GetAudioStatus() == SDL_AUDIO_PLAYING);
                break;
            }
        }
        else if (event.type == SDL_VIDEORESIZE) {
            if (!isFullscreen) {
                int mins = event.resize.w < event.resize.h ? event.resize.w : event.resize.h;
                windowScale = mins / SV_W;
                UpdateWindowSize();
            }
        }
    }
}

void HandleInput(void)
{
    uint8_t controls_state = 0;
    uint8_t *keystate = SDL_GetKeyState(NULL);

    if (keystate[SDLK_RIGHT]) controls_state |= 0x01;
    if (keystate[SDLK_LEFT])  controls_state |= 0x02;
    if (keystate[SDLK_DOWN])  controls_state |= 0x04;
    if (keystate[SDLK_UP])    controls_state |= 0x08;
    if (keystate[SDLK_x])     controls_state |= 0x10;
    if (keystate[SDLK_c])     controls_state |= 0x20;
    if (keystate[SDLK_z])     controls_state |= 0x40;
    if (keystate[SDLK_SPACE]) controls_state |= 0x80;

    supervision_set_input(controls_state);
}

void Draw()
{
    SDL_LockSurface(PrimarySurface);
    uint32_t *pDest = (uint32_t *)PrimarySurface->pixels;
    uint16_t *pSrc = screenBuffer;

    if (isFullscreen) {
        // Center
        pDest += (windowWidth - SV_W * windowScale) / 2 + (windowHeight - SV_H * windowScale) / 2 * windowWidth;
    }
    for (int y = 0; y < SV_H; y++) {
        for (int x = 0; x < SV_W; x++) {
            // RGB555 (R: 0-4 bits) -> BGRA8888 (SDL 32-bit)
            // or SDL_MapRGB()
            uint32_t color = ((*pSrc & 0x7C00) >> (10 - 3))
                           | ((*pSrc & 0x03E0) << ( 3 + 3))
                           | ((*pSrc & 0x001F) << (16 + 3));
            // -> SDL 16-bit
            //((*pSrc & 0x7C00) >> 10) | ((*pSrc & 0x03E0) << 1) | ((*pSrc & 0x001F) << 11);
            pSrc++;
            for (int i = y * windowScale; i < windowScale + y * windowScale; i++) {
                for (int j = x * windowScale; j < windowScale + x * windowScale; j++) {
                    pDest[i * windowWidth + j] = color;
                }
            }
        }
    }

    SDL_UnlockSurface(PrimarySurface);
    SDL_Flip(PrimarySurface);
}

// Audio
void AudioCallback(void *userdata, uint8_t *stream, int len)
{
    supervision_update_sound(stream, len);
}

static double nextTime;
void InitCounter(void)
{
    nextTime = SDL_GetTicks() + 1000.0 / UPDATE_RATE;
}

void Wait(void)
{
    uint32_t now = SDL_GetTicks();
    uint32_t wait = 0;
    if (nextTime <= now) {
        if (now - nextTime > 100.0) {
            nextTime = now;
        }
    }
    else {
        wait = (uint32_t)nextTime - now;
    }
    SDL_Delay(wait);
    nextTime += 1000.0 / UPDATE_RATE;
}

// SDL Fix
#ifdef main
#undef main
#endif

int main(int argc, char *argv[])
{
    OR_DIE(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_JOYSTICK) < 0);

    char title[64] = { 0 };
    snprintf(title, sizeof(title), "Potator (SDL1) %s (core: %u.%u.%u)",
        VERSION, SV_CORE_VERSION_MAJOR, SV_CORE_VERSION_MINOR, SV_CORE_VERSION_PATCH);
    SDL_WM_SetCaption(title, NULL);

    UpdateWindowSize();
    SDL_Rect **modes = SDL_ListModes(NULL, SDL_FULLSCREEN);
    maxWindowWidth  = modes[0]->w;
    maxWindowHeight = modes[0]->h;

    SDL_FillRect(PrimarySurface, NULL, 0);

    SDL_AudioSpec audio_spec;
    memset(&audio_spec, 0, sizeof(audio_spec));
    audio_spec.freq = SV_SAMPLE_RATE;
    audio_spec.channels = 2;
    audio_spec.samples = 512;
    audio_spec.format = AUDIO_S8;
    audio_spec.callback = AudioCallback;
    audio_spec.userdata = NULL;
    OR_DIE(SDL_OpenAudio(&audio_spec, NULL) < 0);

    printf("# Controls\n\
               Right: Right\n\
                Left: Left\n\
                Down: Down\n\
                  Up: Up\n\
                   B: X\n\
                   A: C\n\
              Select: Z\n\
               Start: Space\n\
   Toggle Fullscreen: Return\n\
               Reset: Tab\n\
          Save State: 1\n\
          Load State: 2\n\
        Next Palette: P\n\
Decrease Window Size: -\n\
Increase Window Size: =\n\
   Decrease Ghosting: [\n\
   Increase Ghosting: ]\n\
          Mute Audio: M\n");

    supervision_init();

    if (LoadROM(argc <= 1 ? "rom.sv" : argv[1]) == 0) {
        done = !supervision_load(buffer, bufferSize);
    }
    else {
        done = SDL_TRUE;
    }

    SDL_PauseAudio(0);

    InitCounter();
    while (!done) {
        PollEvents();
        HandleInput();
        supervision_exec(screenBuffer, FALSE);
        Draw();
        Wait();
    }
    supervision_done();

    SDL_CloseAudio();

    SDL_Quit();
}

#define VIDEOMODE_FLAGS (SDL_HWSURFACE | SDL_DOUBLEBUF | SDL_RESIZABLE)

void ToggleFullscreen(void)
{
    static int lastWidth = 0;
    static int lastHeight = 0;
    static int lastScale = 0;

    uint32_t flags = VIDEOMODE_FLAGS;
    if (isFullscreen) {
        windowWidth  = lastWidth;
        windowHeight = lastHeight;
        windowScale  = lastScale;
        SDL_ShowCursor(SDL_ENABLE);
    }
    else {
        lastWidth  = windowWidth;
        lastHeight = windowHeight;
        lastScale  = windowScale;

        flags |= SDL_FULLSCREEN;
        windowScale  = maxWindowHeight / SV_H;
        windowWidth  = maxWindowWidth;
        windowHeight = maxWindowHeight;
        SDL_ShowCursor(SDL_DISABLE);
    }
    PrimarySurface = SDL_SetVideoMode(windowWidth, windowHeight, 32, flags);
    OR_DIE(PrimarySurface == NULL);

    isFullscreen = !isFullscreen;
}

void NextPalette(void)
{
    static int currentPalette = 0;
    currentPalette = (currentPalette + 1) % SV_COLOR_SCHEME_COUNT;
    supervision_set_color_scheme(currentPalette);
}

void UpdateWindowSize(void)
{
    windowWidth  = SV_W * windowScale;
    windowHeight = SV_H * windowScale;
    PrimarySurface = SDL_SetVideoMode(windowWidth, windowHeight, 32, VIDEOMODE_FLAGS);
    OR_DIE(PrimarySurface == NULL);
}

void IncreaseWindowSize(void)
{
    if (isFullscreen)
        return;

    if (SV_W * (windowScale + 1) <= maxWindowWidth && SV_H * (windowScale + 1) <= maxWindowHeight) {
        windowScale++;
        UpdateWindowSize();
    }
}

void DecreaseWindowSize(void)
{
    if (isFullscreen)
        return;

    if (windowScale > 1) {
        windowScale--;
        UpdateWindowSize();
    }
}

void IncreaseGhosting(void)
{
    if (currentGhosting < SV_GHOSTING_MAX) {
        currentGhosting++;
        supervision_set_ghosting(currentGhosting);
    }
}

void DecreaseGhosting(void)
{
    if (currentGhosting > 0) {
        currentGhosting--;
        supervision_set_ghosting(currentGhosting);
    }
}
