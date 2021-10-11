#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

#include "../../common/supervision.h"

#define VERSION "1.0.3"

#define OR_DIE(cond) \
    if (cond) { \
        fprintf(stderr, "[Error] SDL: %s\n", SDL_GetError()); \
        exit(1); \
    }

#define COUNT_OF(x) ((sizeof(x)/sizeof(0[x])) / ((size_t)(!(sizeof(x) % sizeof(0[x])))))

#define UPDATE_RATE 60

typedef enum {
    MENUSTATE_NONE,
    MENUSTATE_DROP_ROM,
    MENUSTATE_EMULATION,
    MENUSTATE_PAUSE,
    MENUSTATE_SET_KEY
} MenuState;

SDL_bool done = SDL_FALSE;
uint16_t screenBuffer[SV_W * SV_H];
SDL_Window *sdlScreen;
SDL_Renderer *sdlRenderer;
SDL_Texture *sdlTexture;

uint8_t *romBuffer;
uint32_t romBufferSize = 0;

SDL_GameController *controller = NULL;

SDL_bool IsFullscreen(void);
void ToggleFullscreen(void);
uint64_t startCounter = 0;
SDL_bool isRefreshRate60 = SDL_FALSE;
void InitCounter(void);
SDL_bool NeedUpdate(void);
void DrawDropROM(void);

int nextMenuState = 0;
#define MENUSTATE_STACK_SIZE 4
MenuState menuStates[MENUSTATE_STACK_SIZE];
void PushMenuState(MenuState state);
void PopMenuState(void);
MenuState GetMenuState(void);

void Reset(void);

int currentPalette = 0;
void NextPalette(void);

int windowScale = 4;
void IncreaseWindowSize(void);
void DecreaseWindowSize(void);

void SaveState(void);
void LoadState(void);

int audioVolume = SDL_MIX_MAXVOLUME;
int lastVolume = -1;
void SetVolume(int volume);
void MuteAudio(void);

int currentGhosting = 0;
void IncreaseGhosting(void);
void DecreaseGhosting(void);
void SetGhosting(int frameCount);

char *keysNames[] = {
    "Right",
    "Left",
    "Down",
    "Up",
    "B",
    "A",
    "Select",
    "Start",

    "Toggle Fullscreen",
    "Reset",
    "Save State",
    "Load State",
    "Next Palette",
    "Decrease Window Size",
    "Increase Window Size",
    "Decrease Ghosting",
    "Increase Ghosting",
    "Mute Audio",
};
// Warning: Emscripten and Android use default values
int keysMapping[] = {
    SDL_SCANCODE_RIGHT,
    SDL_SCANCODE_LEFT,
    SDL_SCANCODE_DOWN,
    SDL_SCANCODE_UP,
    SDL_SCANCODE_X,     // B
    SDL_SCANCODE_C,     // A
    SDL_SCANCODE_Z,     // Select
    SDL_SCANCODE_SPACE, // Start

#ifdef __EMSCRIPTEN__
    SDL_SCANCODE_UNKNOWN,
#else
    SDL_SCANCODE_F11,
#endif
    SDL_SCANCODE_TAB,
    SDL_SCANCODE_1,
    SDL_SCANCODE_2,
    SDL_SCANCODE_P,
    SDL_SCANCODE_MINUS,
    SDL_SCANCODE_EQUALS,
    SDL_SCANCODE_LEFTBRACKET,
    SDL_SCANCODE_RIGHTBRACKET,
    SDL_SCANCODE_M,
};
void (*keysFuncs[])(void) = {
    ToggleFullscreen,
    Reset,
    SaveState,
    LoadState,
    NextPalette,
    DecreaseWindowSize,
    IncreaseWindowSize,
    DecreaseGhosting,
    IncreaseGhosting,
    MuteAudio,
};
int setButton = -1;
void SetKey(int button);

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
    if (romBuffer != NULL) {
        free(romBuffer);
        romBuffer = NULL;
    }

    SDL_RWops *romfile = SDL_RWFromFile(filename, "rb");
    if (romfile == NULL) {
        fprintf(stderr, "SDL_RWFromFile(): Unable to open file!\n");
        return 1;
    }
    romBufferSize = (uint32_t)SDL_RWsize(romfile);
    romBuffer = (uint8_t *)malloc(romBufferSize);
    SDL_RWread(romfile, romBuffer, romBufferSize, 1);
    if (SDL_RWclose(romfile) != 0) {
        fprintf(stderr, "SDL_RWclose(): Unable to close file!\n");
        return 1;
    }
    SetRomName(filename);
    return 0;
}

void ReturnToDropROM(void)
{
    SDL_memset(screenBuffer, 0, sizeof(screenBuffer));
    while (GetMenuState() != MENUSTATE_NONE) {
        PopMenuState();
    }
    PushMenuState(MENUSTATE_DROP_ROM);
    SDL_PauseAudio(1);
}

void LoadBuffer(void)
{
    if (!supervision_load(romBuffer, romBufferSize)) {
        ReturnToDropROM();
        return;
    }
    MenuState prevState = GetMenuState();
    PopMenuState();
    PushMenuState(MENUSTATE_EMULATION);
    if (prevState == MENUSTATE_PAUSE) { // Focus wasn't gained
        PushMenuState(MENUSTATE_PAUSE);
    }
    supervision_set_color_scheme(currentPalette);
    supervision_set_ghosting(currentGhosting);
    SDL_PauseAudio(0);
}

void AudioCallback(void *userdata, uint8_t *stream, int len)
{
    if (GetMenuState() != MENUSTATE_EMULATION) {
        SDL_memset(stream, ((SDL_AudioSpec*)userdata)->silence, len);
        return;
    }

    // U8 to F32
    supervision_update_sound(stream, len / 4);
    float *s = (float*)stream;
    for (int i = len / 4 - 1; i >= 0; i--) {
        // 45 - max
        s[i] = stream[i] / 63.0f * audioVolume / (float)SDL_MIX_MAXVOLUME;
    }

    // Mono
    //for (int i = 0; i < len / 4; i += 2) {
    //    s[i] = s[i + 1] = (s[i] + s[i + 1]) / 2;
    //}

    // U8 or S8. Don't use SDL_PauseAudio() with AUDIO_U8
    /*supervision_update_sound(stream, len);
    for (int i = 0; i < len; i++) {
        stream[i] = (uint8_t)((stream[i] << 1) * audioVolume / (float)SDL_MIX_MAXVOLUME) + ((SDL_AudioSpec*)userdata)->silence;
    }*/
}

void HandleInput(void)
{
    uint8_t controls_state = 0;
    const uint8_t *keystate = SDL_GetKeyboardState(NULL);

    for (int i = 0; i < 8; i++) {
        if (keystate[keysMapping[i]]) {
            controls_state |= 1 << i;
        }
    }

    if (SDL_GameControllerGetAttached(controller)) {
        if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) controls_state |= 0x01;
        if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_LEFT))  controls_state |= 0x02;
        if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_DOWN))  controls_state |= 0x04;
        if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_UP))    controls_state |= 0x08;
        if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_A))          controls_state |= 0x10;
        if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_X))          controls_state |= 0x20;
        if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_BACK))       controls_state |= 0x40;
        if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_START))      controls_state |= 0x80;
        // 31130/32768 == 0.95
        if (SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTX) >  31130) controls_state |= 0x01;
        if (SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTX) < -31130) controls_state |= 0x02;
        if (SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTY) >  31130) controls_state |= 0x04;
        if (SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTY) < -31130) controls_state |= 0x08;
    }

    supervision_set_input(controls_state);
}

#ifdef __ANDROID__
char romPath[1024];
#endif

void PollEvents(void)
{
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) {
            done = SDL_TRUE;
        }
        else if (event.type == SDL_KEYDOWN) {
            if (GetMenuState() == MENUSTATE_SET_KEY) {
                if (event.key.keysym.scancode != SDL_SCANCODE_ESCAPE)
                    keysMapping[setButton] = event.key.keysym.scancode;
                PopMenuState();
                SDL_PauseAudio(0);
                continue;
            }
            for (int i = 0; i < COUNT_OF(keysFuncs); i++) {
                if (keysMapping[i + 8] == event.key.keysym.scancode) {
                    keysFuncs[i]();
                    break;
                }
            }
            if (event.key.keysym.scancode == SDL_SCANCODE_RETURN
                     && (SDL_GetModState() & KMOD_ALT)) {
                ToggleFullscreen();
            }
            if (event.key.keysym.scancode == SDL_SCANCODE_AC_BACK) {
#ifdef __ANDROID__
                supervision_save_state(romPath, 9);
#endif
                done = SDL_TRUE;
            }
        }
        // SDL_CONTROLLERBUTTONDOWN doesn't work
        else if (event.type == SDL_JOYBUTTONDOWN) {
            if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_LEFTSHOULDER)) {
                SaveState();
            }
            else if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)) {
                LoadState();
            }
        }
#ifndef __ANDROID__
        else if (event.type == SDL_WINDOWEVENT) {
            switch (event.window.event) {
            case SDL_WINDOWEVENT_RESIZED:
                if (!IsFullscreen()) {
                    SDL_SetWindowSize(sdlScreen,
                        (event.window.data1 + SV_W / 2) / SV_W * SV_W,
                        (event.window.data2 + SV_H / 2) / SV_H * SV_H);
                }
                break;
            case SDL_WINDOWEVENT_FOCUS_LOST:
                if (GetMenuState() == MENUSTATE_SET_KEY) PopMenuState();
                PushMenuState(MENUSTATE_PAUSE);
                SDL_PauseAudio(1);
                break;
            case SDL_WINDOWEVENT_FOCUS_GAINED:
                if (GetMenuState() == MENUSTATE_PAUSE) PopMenuState();
                SDL_PauseAudio(0);
                break;
            }
        }
#endif
        else if (event.type == SDL_DROPFILE) {
            if (LoadROM(event.drop.file) == 0) {
                LoadBuffer();
            }
            else {
                ReturnToDropROM();
            }
#ifdef __ANDROID__
            // External SD Card isn't writable
            strncpy(romPath, SDL_AndroidGetExternalStoragePath(), sizeof(romPath));
            strncat(romPath, "/", sizeof(romPath) - strlen(romPath) - 1);
            strncat(romPath, romName, sizeof(romPath) - strlen(romPath) - 1);
            supervision_load_state(romPath, 9);
#endif
            SDL_free(event.drop.file);
        }
        else if (event.type == SDL_JOYDEVICEADDED) {
            if (SDL_IsGameController(event.jdevice.which)) {
                controller = SDL_GameControllerOpen(event.jdevice.which);
                if (!controller) {
                    fprintf(stderr, "Could not open gamecontroller %i: %s\n", event.jdevice.which, SDL_GetError());
                }
            }
        }
    }
}

void Loop(void)
{
    PollEvents();

    while (NeedUpdate()) {
        switch (GetMenuState()) {
        case MENUSTATE_EMULATION:
            HandleInput();
            supervision_exec(screenBuffer, FALSE);
            break;
        case MENUSTATE_DROP_ROM:
            DrawDropROM();
            break;
        case MENUSTATE_PAUSE:
            SDL_Delay(16); // Reduce CPU usage
            break;
        case MENUSTATE_SET_KEY:
            break;
        default:
            break;
        }
    }

    // Draw
    SDL_UpdateTexture(sdlTexture, NULL, screenBuffer, SV_W * sizeof(uint16_t));

    SDL_RenderClear(sdlRenderer);
    SDL_RenderCopy(sdlRenderer, sdlTexture, NULL, NULL);
    SDL_RenderPresent(sdlRenderer);
#ifdef __EMSCRIPTEN__
    if (done) {
        emscripten_cancel_main_loop();
    }
#endif
}

int main(int argc, char *argv[])
{
    OR_DIE(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_JOYSTICK) < 0);

    char title[64] = { 0 };
    snprintf(title, sizeof(title), "Potator (SDL2) %s (core: %u.%u.%u)",
        VERSION, SV_CORE_VERSION_MAJOR, SV_CORE_VERSION_MINOR, SV_CORE_VERSION_PATCH);
    sdlScreen = SDL_CreateWindow(title,
        SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED,
        SV_W * windowScale, SV_H * windowScale,
        SDL_WINDOW_RESIZABLE);
    OR_DIE(sdlScreen == NULL);

    sdlRenderer = SDL_CreateRenderer(sdlScreen, -1, SDL_RENDERER_PRESENTVSYNC);
    OR_DIE(sdlRenderer == NULL);

    SDL_RenderSetLogicalSize(sdlRenderer, SV_W, SV_H);

    sdlTexture = SDL_CreateTexture(sdlRenderer,
        SDL_PIXELFORMAT_BGR555,
        SDL_TEXTUREACCESS_STREAMING,
        SV_W, SV_H);
    OR_DIE(sdlTexture == NULL);

    SDL_AudioSpec audio_spec;
    SDL_zero(audio_spec);
    audio_spec.freq = SV_SAMPLE_RATE;
    audio_spec.channels = 2;
    audio_spec.samples = 512;
    audio_spec.format = AUDIO_F32;
    audio_spec.callback = AudioCallback;
    audio_spec.userdata = &audio_spec;
    OR_DIE(SDL_OpenAudio(&audio_spec, NULL) < 0);
    //SDL_AudioDeviceID devid = SDL_OpenAudioDevice(NULL, 0, &audio_spec, NULL, 0);
    //OR_DIE(devid == 0);

    printf("# Controls\n");
    for (int i = 0; i < COUNT_OF(keysNames); i++) {
        printf("%20s: %s\n", keysNames[i], SDL_GetScancodeName(keysMapping[i]));
    }
    printf("\n");

    supervision_init();

    PushMenuState(MENUSTATE_DROP_ROM);
    if (LoadROM(argc <= 1 ? "rom.sv" : argv[1]) == 0) {
        LoadBuffer();
    }

    //SDL_PauseAudio(0);
    //SDL_PauseAudioDevice(devid, 0);

    InitCounter();
#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop(Loop, 0, 1);
#else
    while (!done) {
        Loop();
    }
#endif

    supervision_done();

    SDL_CloseAudio();
    //SDL_CloseAudioDevice(devid);

    SDL_DestroyTexture(sdlTexture);
    SDL_DestroyRenderer(sdlRenderer);
    SDL_DestroyWindow(sdlScreen);

    SDL_Quit();
#ifdef __ANDROID__
    // Unload library hack https://stackoverflow.com/a/6509866
    exit(0);
#endif
    return 0;
}

#define FULLSCREEN_FLAG SDL_WINDOW_FULLSCREEN_DESKTOP

SDL_bool IsFullscreen(void)
{
    return 0 != (SDL_GetWindowFlags(sdlScreen) & FULLSCREEN_FLAG);
}

void ToggleFullscreen(void)
{
    static int mouseX;
    static int mouseY;
    static SDL_bool cursorInWindow = SDL_FALSE;
#ifdef __EMSCRIPTEN__
    return;
#endif
    if (!IsFullscreen()) {
        SDL_SetWindowFullscreen(sdlScreen, FULLSCREEN_FLAG);
        SDL_ShowCursor(SDL_DISABLE);

        int x, y;
        SDL_GetMouseState(&x, &y);
        SDL_GetGlobalMouseState(&mouseX, &mouseY);
        if (x == mouseX && y == mouseY) {
            cursorInWindow = SDL_TRUE;
        }
    }
    else {
        SDL_SetWindowFullscreen(sdlScreen, 0);
        SDL_ShowCursor(SDL_ENABLE);

        // Don't move cursor. Bug?
        if (cursorInWindow) {
            SDL_WarpMouseInWindow(sdlScreen, mouseX, mouseY);
        }
        else {
            SDL_WarpMouseGlobal(mouseX, mouseY);
        }
        cursorInWindow = SDL_FALSE;
    }
}

void InitCounter(void)
{
    SDL_DisplayMode current;
    SDL_GetCurrentDisplayMode(0, &current);
    isRefreshRate60 = (current.refresh_rate == 60);
    startCounter = SDL_GetPerformanceCounter();
}

SDL_bool NeedUpdate(void)
{
    static double elapsedCounter = 0.0;
    static SDL_bool result = SDL_FALSE;

    if (isRefreshRate60) {
        result = !result;
    }
    else {
        // New frame
        if (!result) {
            uint64_t now = SDL_GetPerformanceCounter();
            elapsedCounter += (double)((now - startCounter) * 1000) / SDL_GetPerformanceFrequency();
            startCounter = now;
            if (elapsedCounter > 100.0) {
                elapsedCounter = 0.0;
            }
        }
        result = elapsedCounter >= 1000.0 / UPDATE_RATE;
        if (result) {
            elapsedCounter -= 1000.0 / UPDATE_RATE;
        }
    }
    return result;
}

void DrawDropROM(void)
{
    static uint8_t fade = 0;
    char dropRom[] = {
        "##  ###  ## ###  ###  ## ###"
        "# # # # # # # #  # # # # ###"
        "# # ##  # # ###  ##  # # # #"
        "### # # ##  #    # # ##  # #"
    };
    uint8_t f = (fade < 32) ? fade : 63 - fade;
    uint16_t color = (f << 0) | (f << 5) | (f << 10);
    fade = (fade + 1) % 64;

    int width = 28, height = 4;
    int scale = 4, start = (SV_W - width * scale) / 2 + SV_W * (SV_H - height * scale) / 2;
    for (int j = 0; j < height * scale; j++) {
        for (int i = 0; i < width * scale; i++) {
            if (dropRom[i / scale + width * (j / scale)] == '#')
                screenBuffer[start + i + SV_W * j] = color;
        }
    }
}

void PushMenuState(MenuState state)
{
    if (nextMenuState >= MENUSTATE_STACK_SIZE) {
        // Error
        return;
    }
    if (nextMenuState == 0 || (nextMenuState > 0 && menuStates[nextMenuState - 1] != state)) {
        menuStates[nextMenuState] = state;
        nextMenuState++;
    }
}

void PopMenuState(void)
{
    if (nextMenuState > 0)
        nextMenuState--;
}

MenuState GetMenuState(void)
{
    if (nextMenuState > 0)
        return menuStates[nextMenuState - 1];
    return MENUSTATE_NONE;
}

// Emscripten
void UploadROM(void *newBuffer, int newBufferSize, const char *fileName)
{
    romBufferSize = newBufferSize;
    if (romBuffer != NULL) {
        free(romBuffer);
        romBuffer = NULL;
    }
    romBuffer = (uint8_t *)malloc(romBufferSize);
    if (romBuffer == NULL) {
        return;
    }
    memcpy(romBuffer, newBuffer, romBufferSize);

    SetRomName(fileName);
    LoadBuffer();
}

void Reset(void)
{
    if (GetMenuState() == MENUSTATE_EMULATION)
        LoadBuffer();
}

void NextPalette(void)
{
    currentPalette = (currentPalette + 1) % SV_COLOR_SCHEME_COUNT;
    supervision_set_color_scheme(currentPalette);
}

void IncreaseWindowSize(void)
{
    if (IsFullscreen())
        return;

    SDL_DisplayMode dm;
    SDL_GetDesktopDisplayMode(0, &dm);
    if (SV_W * (windowScale + 1) <= dm.w && SV_H * (windowScale + 1) <= dm.h) {
        windowScale++;
        SDL_SetWindowSize(sdlScreen, SV_W * windowScale, SV_H * windowScale);
    }
}

void DecreaseWindowSize(void)
{
    if (IsFullscreen())
        return;

    if (windowScale > 1) {
        windowScale--;
        SDL_SetWindowSize(sdlScreen, SV_W * windowScale, SV_H * windowScale);
    }
}

void SaveState(void)
{
    if (GetMenuState() == MENUSTATE_EMULATION)
        supervision_save_state(romName, 0);
}

void LoadState(void)
{
    if (GetMenuState() == MENUSTATE_EMULATION)
        supervision_load_state(romName, 0);
}

void SetVolume(int volume)
{
    if (volume < 0)
        audioVolume = 0;
    else if (volume > SDL_MIX_MAXVOLUME)
        audioVolume = SDL_MIX_MAXVOLUME; // 128
    else
        audioVolume = volume;
    if (lastVolume != -1) { // Mute
        lastVolume = audioVolume;
        audioVolume = 0;
    }
}

void MuteAudio(void)
{
    if (lastVolume == -1) {
        lastVolume = audioVolume;
        audioVolume = 0;
    }
    else {
        audioVolume = lastVolume;
        lastVolume = -1;
    }
}

void IncreaseGhosting(void)
{
    SetGhosting(currentGhosting + 1);
}

void DecreaseGhosting(void)
{
    SetGhosting(currentGhosting - 1);
}

void SetGhosting(int frameCount)
{
    currentGhosting = frameCount;
    if (frameCount < 0)
        currentGhosting = 0;
    else if (frameCount > SV_GHOSTING_MAX)
        currentGhosting = SV_GHOSTING_MAX;
    if (frameCount == currentGhosting) {
        supervision_set_ghosting(currentGhosting);
    }
}

void SetKey(int button)
{
    setButton = button;
    PushMenuState(MENUSTATE_SET_KEY);
    SDL_PauseAudio(1);
}
