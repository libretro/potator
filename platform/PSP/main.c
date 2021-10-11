#include <pspdebug.h>
#include <pspkernel.h>
#include <pspdisplay.h>
#include <pspgu.h>
#include <psprtc.h>
#include <pspctrl.h>
#include <psppower.h>
#include <pspaudio.h>
#include <pspaudiolib.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/unistd.h>

#include <intraFont.h>

#include "callbacks.h"
#include "vram.h"

#include "../../common/supervision.h"

#define VERSION "1.0.3"

PSP_MODULE_INFO("Potator", PSP_MODULE_USER, 1, 1);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER);

#define COUNT_OF(x) ((sizeof(x)/sizeof(0[x])) / ((size_t)(!(sizeof(x) % sizeof(0[x])))))

#define BUF_WIDTH  512
#define SCR_WIDTH  480
#define SCR_HEIGHT 272
#define TEX_WIDTH  256

// GU_PSM_5551 - a bit faster (?)
#define PIX_FORMAT GU_PSM_8888

static unsigned int __attribute__((aligned(16))) list[262144];
static uint32_t __attribute__((aligned(16))) pixels[BUF_WIDTH * SCR_HEIGHT];
static uint32_t __attribute__((aligned(16))) pixelsPreview[BUF_WIDTH * SCR_HEIGHT];

uint8_t* romBuffer = NULL;
uint32_t romBufferSize = 0;

void UnloadROM(void)
{
    free(romBuffer); romBuffer = NULL;
}

int LoadROM(const char* filename)
{
    if (romBuffer != NULL) {
        UnloadROM();
    }

    FILE* romfile = fopen(filename, "rb");
    if (romfile == NULL) {
        printf("fopen(): Unable to open file!\n");
        return 1;
    }
    fseek(romfile, 0, SEEK_END);
    romBufferSize = ftell(romfile);
    fseek(romfile, 0, SEEK_SET);

    romBuffer = (uint8_t*)malloc(romBufferSize);

    fread(romBuffer, romBufferSize, 1, romfile);

    if (fclose(romfile) == EOF) {
        printf("fclose(): Unable to close file!\n");
        return 1;
    }
    return 0;
}

void HandleInput()
{
    SceCtrlData pad;
    sceCtrlPeekBufferPositive(&pad, 1);

    uint8_t controls_state = 0;
    if (pad.Buttons & PSP_CTRL_RIGHT ) controls_state |= 0x01;
    if (pad.Buttons & PSP_CTRL_LEFT  ) controls_state |= 0x02;
    if (pad.Buttons & PSP_CTRL_DOWN  ) controls_state |= 0x04;
    if (pad.Buttons & PSP_CTRL_UP    ) controls_state |= 0x08;
    if (pad.Buttons & PSP_CTRL_CROSS ) controls_state |= 0x10;
    if (pad.Buttons & PSP_CTRL_SQUARE) controls_state |= 0x20;
    if (pad.Buttons & PSP_CTRL_SELECT) controls_state |= 0x40;
    if (pad.Buttons & PSP_CTRL_START ) controls_state |= 0x80;

    supervision_set_input(controls_state);
}

void AudioStreamCallback(void* buf, unsigned int length, void* userdata)
{
    length <<= 1;
    supervision_update_sound((uint8_t*)buf, length);
    int16_t* dst = (int16_t*)buf;
    uint8_t* src = (uint8_t*)buf;
    int i;
    for (i = length - 1; i >= 0; i--) {
        dst[i] = src[i] << (8 + 1);
    }
}

void SetAudio(int enable)
{
    static int enabled = 0;
    if (!enabled && enable) {
        pspAudioSetChannelCallback(0, AudioStreamCallback, NULL);
    }
    else if (enabled && !enable) {
        pspAudioSetChannelCallback(0, NULL, NULL);
    }
    enabled = enable;
}

// Files

SceIoDirent dir;
char romName[MAXPATHLEN];
char curPath[MAXPATHLEN];
char saveStatesPath[MAXPATHLEN];
char fileNames[1024][MAXPATHLEN]; // 1MB
int firstFileIndex = 0;

int GetFileNames(const char* root, int offset);

// Menu

typedef enum {
    FUNC_ACTION_NONE,
    FUNC_ACTION_PUSH,
    FUNC_ACTION_POP,
    FUNC_ACTION_EMULATION,
} FUNC_ACTION;

typedef void (*MenuInsideFunc)();
typedef FUNC_ACTION (*MenuFunc)(int, MenuInsideFunc*, SceCtrlData);

void InitGu(void);
void Menu_init(void);
int Menu(void);

FUNC_ACTION Menu_main(int stage, MenuFunc* next, SceCtrlData newPad);
FUNC_ACTION Menu_browser(int stage, MenuFunc* next, SceCtrlData newPad);
FUNC_ACTION Menu_options(int stage, MenuFunc* next, SceCtrlData newPad);
FUNC_ACTION Menu_help(int stage, MenuFunc* next, SceCtrlData newPad);
FUNC_ACTION Menu_about(int stage, MenuFunc* next, SceCtrlData newPad);

// Fonts

enum colors {
    RED        = 0xFF0000FF,
    GREEN      = 0xFF00FF00,
    BLUE       = 0xFFFF0000,
    YELLOW     = 0xFF00FFFF,
    DARKYELLOW = 0xFF3F6F6F,
    WHITE      = 0xFFFFFFFF,
    LITEGRAY   = 0xFFBFBFBF,
    GRAY       = 0xFF7F7F7F,
    DARKGRAY   = 0xFF3F3F3F,
    BLACK      = 0xFF000000,
};
intraFont* ltn[2];

// Options

void textClockFrequency(char* buf, int size);
void prevClockFrequency(void);
void nextClockFrequency(void);
BOOL showFPS = FALSE;
void textFPS(char* buf, int size);
void nextFPS(void);
int currPalette = 0;
void textPalette(char* buf, int size);
void prevPalette(void);
void nextPalette(void);
BOOL muteAudio = FALSE;
void textMute(char* buf, int size);
void nextMute(void);
BOOL enabledVSync = FALSE;
void textVSync(char* buf, int size);
void nextVSync(void);

typedef struct {
    void (*text)(char* buf, int size);
    void (*prev)(void);
    void (*next)(void);
} Option;

Option options[] = {
    { textClockFrequency, prevClockFrequency, nextClockFrequency },
    { textFPS, nextFPS, nextFPS },
    { textPalette, prevPalette, nextPalette },
    { textMute, nextMute, nextMute },
    { textVSync, nextVSync, nextVSync },
};

// Graphics

struct VertexF
{
    float u, v;
    float x, y, z;
};

void Blit(int dx, int dy, int dw, int dh);
void DrawButton(int button, int x, int y);

// Framebuffers

void* fbpc;
void* fbp0;
void* fbp1;
void* zbp;

int main(int argc, char* argv[])
{
    pspDebugScreenInit();
    pspDebugScreenSetColorMode(PIX_FORMAT);
    setupCallbacks();

    // Setup GU

    fbp0 = getStaticVramBuffer(BUF_WIDTH, SCR_HEIGHT, PIX_FORMAT);
    fbp1 = getStaticVramBuffer(BUF_WIDTH, SCR_HEIGHT, PIX_FORMAT);
    zbp  = getStaticVramBuffer(BUF_WIDTH, SCR_HEIGHT, GU_PSM_4444);
    fbpc = fbp0;

    // InitGu();

    // Flush caches to make sure no stray data remains
    sceKernelDcacheWritebackAll();

    SceCtrlData oldPad;
    oldPad.Buttons = 0;
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_DIGITAL);

    float curr_fps = 0.0f;
    uint64_t last_tick;
    sceRtcGetCurrentTick(&last_tick);
    uint32_t tick_res = sceRtcGetTickResolution();
    int frame_count = 0;

    pspAudioInit();

    supervision_init();

    //SetAudio(1);

    getcwd(saveStatesPath, sizeof(saveStatesPath));
    strcat(saveStatesPath, "/SaveStates");
    sceIoMkdir(saveStatesPath, 0777);

    BOOL isMenu = TRUE;

    while (running()) {
        HandleInput();

        SceCtrlData pad, newPad;
        // sceCtrlReadBufferPositive (unlike sceCtrlPeekBufferPositive) limit frames to 60
        if (sceCtrlReadBufferPositive(&pad, 1)) {
            newPad.Buttons = ~oldPad.Buttons & pad.Buttons;
            if ((newPad.Buttons & PSP_CTRL_CIRCLE)
             || (newPad.Buttons & PSP_CTRL_HOME)) {
                isMenu = TRUE;
            }
            if (newPad.Buttons & PSP_CTRL_LTRIGGER) {
                nextPalette();
            }
            oldPad = pad;
        }

        if (isMenu) {
            SetAudio(0);
            if (!Menu()) {
                break;
            }
            SetAudio(!muteAudio);
            isMenu = FALSE;
            memset(&oldPad, 0xFF, sizeof(SceCtrlData)); // Reset buttons
        }

        supervision_exec_ex((uint16_t*)pixels, TEX_WIDTH, FALSE);

        sceKernelDcacheWritebackAll();

        // Begin rendering

        sceGuStart(GU_DIRECT, list);

        sceGuTexMode(GU_PSM_5551, 0, 0, GU_FALSE);
        sceGuTexImage(0, TEX_WIDTH, TEX_WIDTH, TEX_WIDTH, pixels);
        sceGuTexFunc(GU_TFX_REPLACE, GU_TCC_RGBA); // Don't get influenced by any vertex colors
        sceGuTexFilter(GU_LINEAR, GU_LINEAR);

        // (SCR_WIDTH - SCR_HEIGHT) / 2 == 104
        Blit(104, 0, 272, SCR_HEIGHT);

        sceGuFinish();
        sceGuSync(0, 0);

        if (showFPS) {
            uint64_t curr_tick;
            sceRtcGetCurrentTick(&curr_tick);
            frame_count++;
            if ((curr_tick - last_tick) >= tick_res) {
                float time_span = (curr_tick - last_tick) / (float)tick_res;
                curr_fps = frame_count / time_span;
                last_tick = curr_tick; //sceRtcGetCurrentTick(&last_tick);
                frame_count = 0;
            }

            pspDebugScreenSetOffset((int)fbpc);
            pspDebugScreenSetXY(0, 0);
            pspDebugScreenPrintf("%3.03f", curr_fps);
        }

        if (enabledVSync)
            sceDisplayWaitVblankStart();
        fbpc = sceGuSwapBuffers();
    }

    SetAudio(0);

    supervision_done();

    pspAudioEnd();

    int i;
    for (i = 0; i < 2; i++) {
        intraFontUnload(ltn[i]);
    }
    intraFontShutdown();

    sceGuTerm();
    sceKernelExitGame();
    return 0;
}

// Files

int FileNamesCompare(const void* x1, const void* x2)
{
    return strcasecmp((char*)x1, (char*)x2);
}

int GetFileNames(const char* root, int offset)
{
    int dfd, count = 0;
    int i;
    for (i = 0; i < 2; i++) {
        dfd = sceIoDopen(root);
        if (dfd >= 0) {
            while (sceIoDread(dfd, &dir) > 0) {
                if (FIO_SO_ISDIR(dir.d_stat.st_attr)) {
                    if (i == 0) {
                        if (dir.d_name[0] != '.' || dir.d_name[1] != '\0') {
                            strcpy(fileNames[count], dir.d_name);
                            count++;
                        }
                    }
                }
                else if (i == 1) {
                    int startExt = strlen(dir.d_name) - 4;
                    if (startExt >= 0 && (
                        strcasecmp(dir.d_name + startExt + 1, ".sv") == 0
                     || strcasecmp(dir.d_name + startExt + 1, ".ws") == 0
                     || strcasecmp(dir.d_name + startExt,    ".bin") == 0)
                       ) {
                        strcpy(fileNames[count], dir.d_name);
                        count++;
                    }
                }
            }
            sceIoDclose(dfd);
        }
        if (i == 0) {
            // Folders
            firstFileIndex = count;
            qsort(&fileNames[0], count, MAXPATHLEN, FileNamesCompare);
        }
        else {
            // Files
            qsort(&fileNames[firstFileIndex], count - firstFileIndex, MAXPATHLEN, FileNamesCompare);
        }
    }
    return count;
}

// ...

void InitGu(void)
{
    sceGuTerm();
    sceGuInit();

    sceGuStart(GU_DIRECT, list);
    sceGuDrawBuffer(PIX_FORMAT, fbp0, BUF_WIDTH);
    sceGuDispBuffer(SCR_WIDTH, SCR_HEIGHT, fbp1, BUF_WIDTH);
    sceGuDepthBuffer(zbp, BUF_WIDTH);
    sceGuEnable(GU_TEXTURE_2D);
    sceGuOffset(0, 0);
    sceGuViewport(SCR_WIDTH/2, SCR_HEIGHT/2, SCR_WIDTH, SCR_HEIGHT);
    sceGuDepthRange(65535, 0);
    sceGuDepthMask(0xffff);
    sceGuScissor(0, 0, SCR_WIDTH, SCR_HEIGHT);
    sceGuEnable(GU_SCISSOR_TEST);
    sceGuDisable(GU_ALPHA_TEST);
    sceGuDisable(GU_BLEND);
    sceGuDisable(GU_DEPTH_TEST);
    sceGuDisable(GU_LIGHTING);
    sceGuFrontFace(GU_CW);
    sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);

    sceGuFinish();
    sceGuSync(0, 0);

    sceDisplayWaitVblankStart();
    sceGuDisplay(GU_TRUE);

    memset(pixels, 0, sizeof(pixels));
}

// Menu

void Menu_init(void)
{
    sceGuTerm();
    sceGuInit();

    static BOOL loaded = FALSE;
    if (!loaded) {
        intraFontInit();

        char file[40];
        int i;
        for (i = 0; i < 2; i++) {
            sprintf(file, "flash0:/font/ltn%d.pgf", i * 8); // Only 0 and 8
            ltn[i] = intraFontLoad(file, 0); // <- this is where the actual loading happens 
            intraFontSetStyle(ltn[i], 1.0f, WHITE, DARKGRAY, 0);
            //pspDebugScreenSetXY(15,2);
            //pspDebugScreenPrintf("%d%%",(i+1)*100/20);
        }
        loaded = TRUE;
    }

    sceGuStart(GU_DIRECT, list);
    sceGuDrawBuffer(PIX_FORMAT, fbp0, BUF_WIDTH);
    sceGuDispBuffer(SCR_WIDTH, SCR_HEIGHT, fbp1, BUF_WIDTH);
    sceGuDepthBuffer(zbp, BUF_WIDTH);
    sceGuOffset(2048 - (SCR_WIDTH/2), 2048 - (SCR_HEIGHT/2));
    sceGuViewport(2048, 2048, SCR_WIDTH, SCR_HEIGHT);
    sceGuDepthRange(65535, 0);
    sceGuScissor(0, 0, SCR_WIDTH, SCR_HEIGHT);
    sceGuEnable(GU_SCISSOR_TEST);
    sceGuFrontFace(GU_CW);
    sceGuEnable(GU_DEPTH_TEST);
    sceGuDepthFunc(GU_GEQUAL);
    sceGuShadeModel(GU_SMOOTH);
    sceGuEnable(GU_CULL_FACE);
    sceGuEnable(GU_CLIP_PLANES);
    sceGuEnable(GU_BLEND);
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);

    sceGuFinish();
    sceGuSync(0, 0);

    sceDisplayWaitVblankStart();
    sceGuDisplay(GU_TRUE);
}

// \return 0 - exit, 1 - continue emulation
int Menu(void)
{
    Menu_init();

    SceCtrlData pad, oldPad, newPad;
    memset(&oldPad, 0xFF, sizeof(SceCtrlData));
    memset(&newPad, 0x00, sizeof(SceCtrlData));

    FUNC_ACTION result;
    int stackIndex = 0;
    MenuFunc stack[4] = { (MenuFunc)Menu_main };
    MenuFunc nextFunc = NULL;
    stack[stackIndex](0, NULL, newPad);
    while (running()) {
        sceCtrlPeekBufferPositive(&pad, 1);
        newPad.Buttons = ~oldPad.Buttons & pad.Buttons;

        // Repeating
        static int counter = 0;
        static int delay = 0;
        if (pad.Buttons & PSP_CTRL_DOWN || pad.Buttons & PSP_CTRL_UP) {
            counter++;
            if (counter > delay) {
                newPad.Buttons |= (pad.Buttons & PSP_CTRL_DOWN) | (pad.Buttons & PSP_CTRL_UP);
                counter = 0;
                delay = 2;
            }
        }
        else {
            counter = 0;
            delay = 10;
        }

        sceGuStart(GU_DIRECT, list);
        sceGuClearColor(GRAY);
        sceGuClearDepth(0);
        sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);

        result = stack[stackIndex](1, (MenuInsideFunc*)&nextFunc, newPad);
        switch (result) {
        case FUNC_ACTION_PUSH:
            stackIndex++;
            stack[stackIndex] = nextFunc;
            stack[stackIndex](0, NULL, newPad);
            break;
        case FUNC_ACTION_POP:
            stack[stackIndex] = (MenuFunc)NULL;
            stackIndex--;
            break;
        case FUNC_ACTION_EMULATION:
            // Clear fb0, fb1
            sceGuClearColor(0);
            sceGuClearDepth(0);
            sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);
            sceGuFinish();
            sceGuSync(0, 0);

            //sceDisplayWaitVblankStart();
            sceGuSwapBuffers();

            sceGuStart(GU_DIRECT, list);
            sceGuClearColor(0);
            sceGuClearDepth(0);
            sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);
            sceGuFinish();
            sceGuSync(0, 0);

            InitGu();
            return 1;
        default:
            break;
        }

        oldPad = pad;

        sceGuFinish();
        sceGuSync(0,0);

        sceDisplayWaitVblankStart();
        fbpc = sceGuSwapBuffers();
    }
    return 0;
}

#define MENU_LINE_HEIGHT 16
#define MENU_PADDING_LEFT 8
#define MENU_PADDING_TOP 20
#define FILE_ROWS ((SCR_HEIGHT - MENU_PADDING_TOP) / MENU_LINE_HEIGHT)

FUNC_ACTION Menu_main(int stage, MenuFunc* next, SceCtrlData newPad)
{
    static const int entryCount = 7;
    static int count = 0;
    static BOOL areYouSure = FALSE;

    if (stage == 0) {
        count = 0;
        areYouSure = FALSE;
        // Game screen preview
        uint32_t* pDest = pixelsPreview; // GU_PSM_8888
        uint16_t* pSrc  = (uint16_t*)pixels;
        int y, x;
        for (y = 0; y < SV_H; y++) {
            for (x = 0; x < SV_W; x++) {
                *pDest = ((*pSrc & 0x1f) << 3) | ((*pSrc & 0x3e0) << (3 + 3)) | ((*pSrc & 0x7c00) << (6 + 3));
                pDest++;
                pSrc++;
            }
            pDest += (TEX_WIDTH - SV_W);
            pSrc  += (TEX_WIDTH - SV_W);
        }
    }
    else {
        sceGuCopyImage(PIX_FORMAT, 0, 0, SV_W, SV_H, TEX_WIDTH, pixelsPreview,
            SCR_WIDTH - SV_W, SCR_HEIGHT - SV_H, BUF_WIDTH, (void*)(((unsigned int)fbpc) + 0x4000000));

        intraFontPrint(ltn[0], MENU_PADDING_LEFT +  0, MENU_PADDING_TOP, "Main Menu");
        intraFontPrint(ltn[1], MENU_PADDING_LEFT + 10, MENU_PADDING_TOP + MENU_LINE_HEIGHT * 1, "Load ROM");
        intraFontPrint(ltn[1], MENU_PADDING_LEFT + 10, MENU_PADDING_TOP + MENU_LINE_HEIGHT * 2, "Save State");
        intraFontPrint(ltn[1], MENU_PADDING_LEFT + 10, MENU_PADDING_TOP + MENU_LINE_HEIGHT * 3, "Load State");
        intraFontPrint(ltn[1], MENU_PADDING_LEFT + 10, MENU_PADDING_TOP + MENU_LINE_HEIGHT * 4, "Reset");
        if (areYouSure) {
            DrawButton(PSP_CTRL_TRIANGLE, (SCR_WIDTH - 16) / 2, SCR_HEIGHT / 2);
            intraFontSetStyle(ltn[0], 1.0f, WHITE, RED, INTRAFONT_ALIGN_CENTER);
            intraFontPrint(ltn[0], SCR_WIDTH / 2, SCR_HEIGHT / 2, "Are You Sure?");
            intraFontSetStyle(ltn[0], 1.0f, WHITE, DARKGRAY, 0);
        }
        intraFontPrint(ltn[1], MENU_PADDING_LEFT + 10, MENU_PADDING_TOP + MENU_LINE_HEIGHT * 5, "Options");
        intraFontPrint(ltn[1], MENU_PADDING_LEFT + 10, MENU_PADDING_TOP + MENU_LINE_HEIGHT * 6, "Help");
        intraFontPrint(ltn[1], MENU_PADDING_LEFT + 10, MENU_PADDING_TOP + MENU_LINE_HEIGHT * 7, "About");
        intraFontPrint(ltn[1], MENU_PADDING_LEFT +  0, MENU_PADDING_TOP + MENU_LINE_HEIGHT * (count + 1), ">");

        if (newPad.Buttons & PSP_CTRL_CROSS) {
            switch (count) {
            case 0:
                *next = (MenuFunc)Menu_browser;
                return FUNC_ACTION_PUSH;
            case 1:
                if (romBuffer) {
                    chdir(saveStatesPath);
                    supervision_save_state(romName, 0);
                    chdir(curPath);
                }
                break;
            case 2:
                if (romBuffer) {
                    chdir(saveStatesPath);
                    supervision_load_state(romName, 0);
                    chdir(curPath);
                }
                break;
            case 3:
                if (romBuffer) {
                    areYouSure = TRUE;
                }
                break;
            case 4:
                *next = (MenuFunc)Menu_options;
                return FUNC_ACTION_PUSH;
            case 5:
                *next = (MenuFunc)Menu_help;
                return FUNC_ACTION_PUSH;
            case 6:
                *next = (MenuFunc)Menu_about;
                return FUNC_ACTION_PUSH;
            }
        }
        else if (newPad.Buttons & PSP_CTRL_CIRCLE) {
            if (romBuffer) {
                return FUNC_ACTION_EMULATION;
            }
        }
        else if (newPad.Buttons & PSP_CTRL_UP) {
            count--;
            if (count < 0) count = entryCount - 1;
            areYouSure = FALSE;
        }
        else if (newPad.Buttons & PSP_CTRL_DOWN) {
            count = (count + 1) % entryCount;
            areYouSure = FALSE;
        }
        else if (newPad.Buttons & PSP_CTRL_TRIANGLE) {
            if (count == 3 && areYouSure) {
                supervision_reset();
                supervision_set_color_scheme(currPalette);
                return FUNC_ACTION_EMULATION;
            }
        }
    }
    return FUNC_ACTION_NONE;
}

FUNC_ACTION Menu_browser(int stage, MenuFunc* next, SceCtrlData newPad)
{
    static int entryCount = 0;
    static int count = 0;

    if (stage == 0) {
        getcwd(curPath, MAXPATHLEN);
        strcat(curPath, "/");
        entryCount = GetFileNames(curPath, 0);

        count = 0;
    }
    else {
        intraFontPrint(ltn[1], MENU_PADDING_LEFT, MENU_PADDING_TOP, curPath);
        intraFontPrint(ltn[1], MENU_PADDING_LEFT, MENU_PADDING_TOP + MENU_LINE_HEIGHT * (count % FILE_ROWS + 1), ">");
        int i, x;
        intraFontSetStyle(ltn[1], 1.0f, YELLOW, DARKYELLOW, 0);
        for (x = 0, i = count / FILE_ROWS * FILE_ROWS; i < count / FILE_ROWS * FILE_ROWS + FILE_ROWS && i < entryCount; x++, i++) {
            if (i < firstFileIndex) {
                intraFontPrint(ltn[1], MENU_PADDING_LEFT + 10 + 2, MENU_PADDING_TOP + MENU_LINE_HEIGHT * (x + 1), "/");
            }
            else {
                intraFontSetStyle(ltn[1], 1.0f, WHITE, DARKGRAY, 0);
            }
            intraFontPrint(ltn[1], MENU_PADDING_LEFT + 20, MENU_PADDING_TOP + MENU_LINE_HEIGHT * (x + 1), fileNames[i]);
        }
        intraFontSetStyle(ltn[1], 1.0f, WHITE, DARKGRAY, 0);

        if (newPad.Buttons & PSP_CTRL_CROSS) {
            if (count >= firstFileIndex) {
                strcpy(romName, fileNames[count]);

                LoadROM(romName);
                if (!supervision_load(romBuffer, romBufferSize)) {
                    UnloadROM();
                    return FUNC_ACTION_NONE;
                }
                supervision_set_color_scheme(currPalette);
                return FUNC_ACTION_EMULATION;
            }

            strcat(curPath, fileNames[count]);
            strcat(curPath, "/");

            if (chdir(curPath) >= 0) {
                getcwd(curPath, MAXPATHLEN);
                strcat(curPath, "/");
                entryCount = GetFileNames(curPath, 0);

                count = 0;
            }
        }
        else if (newPad.Buttons & PSP_CTRL_CIRCLE) {
            return FUNC_ACTION_POP;
        }
        else if (newPad.Buttons & PSP_CTRL_UP) {
            count--;
            if (count < 0) count = entryCount - 1;
        }
        else if (newPad.Buttons & PSP_CTRL_DOWN) {
            count = (count + 1) % entryCount;
        }
    }
    return FUNC_ACTION_NONE;
}

FUNC_ACTION Menu_options(int stage, MenuFunc* next, SceCtrlData newPad)
{
    static int count = 0;
    static int maxTextWidth = 0;
    static char buf[32];

    if (stage == 0) {
        count = 0;

        if (maxTextWidth == 0) {
            int i;
            for (i = 0; i < COUNT_OF(options); i++) {
                options[i].text(buf, sizeof(buf));
                char* p = strchr(buf, ':');
                *p = '\0';
                int w = intraFontMeasureText(ltn[1], buf);
                if (maxTextWidth < w) {
                    maxTextWidth = w;
                }
            }
        }
    }
    else {
        intraFontPrint(ltn[0], MENU_PADDING_LEFT, MENU_PADDING_TOP, "Options");
        int i;
        for (i = 0; i < COUNT_OF(options); i++) {
            options[i].text(buf, sizeof(buf));
            char* p = strchr(buf, ':');
            *p = '\0';
            intraFontPrint(ltn[1], MENU_PADDING_LEFT + 10, MENU_PADDING_TOP + MENU_LINE_HEIGHT * (i + 1), buf);
            p++;
            intraFontPrint(ltn[1], MENU_PADDING_LEFT + 10 + maxTextWidth + 8, MENU_PADDING_TOP + MENU_LINE_HEIGHT * (i + 1), p);
        }
        intraFontPrint(ltn[1], MENU_PADDING_LEFT, MENU_PADDING_TOP + MENU_LINE_HEIGHT* (count + 1), ">");

        if (newPad.Buttons & PSP_CTRL_CROSS) {
            options[count].next();
        }
        else if (newPad.Buttons & PSP_CTRL_CIRCLE) {
            return FUNC_ACTION_POP;
        }
        else if (newPad.Buttons & PSP_CTRL_UP) {
            count--;
            if (count < 0) count = COUNT_OF(options) - 1;
        }
        else if (newPad.Buttons & PSP_CTRL_DOWN) {
            count = (count + 1) % COUNT_OF(options);
        }
        else if (newPad.Buttons & PSP_CTRL_LEFT) {
            options[count].prev();
        }
        else if (newPad.Buttons & PSP_CTRL_RIGHT) {
            options[count].next();
        }
    }
    return FUNC_ACTION_NONE;
}

FUNC_ACTION Menu_help(int stage, MenuFunc* next, SceCtrlData newPad)
{
    if (stage == 0) {
    }
    else {
        DrawButton(PSP_CTRL_CROSS , SCR_WIDTH / 2, MENU_PADDING_TOP + MENU_LINE_HEIGHT * 4 - 12);
        DrawButton(PSP_CTRL_SQUARE, SCR_WIDTH / 2, MENU_PADDING_TOP + MENU_LINE_HEIGHT * 5 - 12);
        DrawButton(PSP_CTRL_CIRCLE, SCR_WIDTH / 2, MENU_PADDING_TOP + MENU_LINE_HEIGHT * 8 - 12);

        intraFontPrint(ltn[0], MENU_PADDING_LEFT +  0, MENU_PADDING_TOP, "Help");
        intraFontPrint(ltn[1], MENU_PADDING_LEFT + 10, MENU_PADDING_TOP + MENU_LINE_HEIGHT * 1, "In-Game Controls");
        intraFontSetStyle(ltn[1], 1.0f, WHITE, DARKGRAY, INTRAFONT_ALIGN_RIGHT);
        intraFontPrint(ltn[1], SCR_WIDTH / 2 - 8, MENU_PADDING_TOP + MENU_LINE_HEIGHT * 2,       "Action");
        intraFontPrint(ltn[1], SCR_WIDTH / 2 - 8, MENU_PADDING_TOP + MENU_LINE_HEIGHT * 3,        "D-Pad");
        intraFontPrint(ltn[1], SCR_WIDTH / 2 - 8, MENU_PADDING_TOP + MENU_LINE_HEIGHT * 4,            "B");
        intraFontPrint(ltn[1], SCR_WIDTH / 2 - 8, MENU_PADDING_TOP + MENU_LINE_HEIGHT * 5,            "A");
        intraFontPrint(ltn[1], SCR_WIDTH / 2 - 8, MENU_PADDING_TOP + MENU_LINE_HEIGHT * 6,       "Select");
        intraFontPrint(ltn[1], SCR_WIDTH / 2 - 8, MENU_PADDING_TOP + MENU_LINE_HEIGHT * 7,  "Start/Pause");
        intraFontPrint(ltn[1], SCR_WIDTH / 2 - 8, MENU_PADDING_TOP + MENU_LINE_HEIGHT * 8,    "Main Menu");
        intraFontPrint(ltn[1], SCR_WIDTH / 2 - 8, MENU_PADDING_TOP + MENU_LINE_HEIGHT * 9, "Next Palette");
        intraFontSetStyle(ltn[1], 1.0f, WHITE, DARKGRAY, 0);
        intraFontPrint(ltn[1], SCR_WIDTH / 2, MENU_PADDING_TOP + MENU_LINE_HEIGHT * 2, "Button");
        intraFontPrint(ltn[1], SCR_WIDTH / 2, MENU_PADDING_TOP + MENU_LINE_HEIGHT * 3, "D-Pad");
        //intraFontPrint(ltn[1], SCR_WIDTH / 2, MENU_PADDING_TOP + MENU_LINE_HEIGHT * 4, "X");
        //intraFontPrint(ltn[1], SCR_WIDTH / 2, MENU_PADDING_TOP + MENU_LINE_HEIGHT * 5, "[]");
        intraFontPrint(ltn[1], SCR_WIDTH / 2, MENU_PADDING_TOP + MENU_LINE_HEIGHT * 6, "Select");
        intraFontPrint(ltn[1], SCR_WIDTH / 2, MENU_PADDING_TOP + MENU_LINE_HEIGHT * 7, "Start");
        //intraFontPrint(ltn[1], SCR_WIDTH / 2, MENU_PADDING_TOP + MENU_LINE_HEIGHT * 8, "O");
        intraFontPrint(ltn[1], SCR_WIDTH / 2, MENU_PADDING_TOP + MENU_LINE_HEIGHT * 9, "L");

        if (newPad.Buttons & PSP_CTRL_CIRCLE) {
            return FUNC_ACTION_POP;
        }
    }
    return FUNC_ACTION_NONE;
}

FUNC_ACTION Menu_about(int stage, MenuFunc* next, SceCtrlData newPad)
{
    static char version[64];

    if (stage == 0) {
        snprintf(version, sizeof(version), "Version: %s (core: %u.%u.%u)",
            VERSION, SV_CORE_VERSION_MAJOR, SV_CORE_VERSION_MINOR, SV_CORE_VERSION_PATCH);
    }
    else {
        intraFontPrint(ltn[0], MENU_PADDING_LEFT +  0, MENU_PADDING_TOP, "About");
        intraFontPrint(ltn[1], MENU_PADDING_LEFT + 10, MENU_PADDING_TOP + MENU_LINE_HEIGHT * 1, "Potator (PSP) - Watara Supervision Emulator");
        intraFontPrint(ltn[1], MENU_PADDING_LEFT + 10, MENU_PADDING_TOP + MENU_LINE_HEIGHT * 2, version);
        intraFontPrint(ltn[1], MENU_PADDING_LEFT + 10, MENU_PADDING_TOP + MENU_LINE_HEIGHT * 3, "Source Code: https://github.com/infval/potator");

        if (newPad.Buttons & PSP_CTRL_CIRCLE) {
            return FUNC_ACTION_POP;
        }
    }
    return FUNC_ACTION_NONE;
}

// Options

int currClockFrequency = 0;
int clockFrequency[] = { 222, 266, 300, 333 };

void textClockFrequency(char* buf, int size)
{
    int clock = clockFrequency[currClockFrequency];
    snprintf(buf, size, "CPU: %d/%d MHz", clock, clock / 2);
}

void prevClockFrequency(void)
{
    currClockFrequency = currClockFrequency - 1;
    if (currClockFrequency < 0) currClockFrequency = COUNT_OF(clockFrequency) - 1;
    int clock = clockFrequency[currClockFrequency];
    scePowerSetClockFrequency(clock, clock, clock / 2);
}

void nextClockFrequency(void)
{
    currClockFrequency = (currClockFrequency + 1) % COUNT_OF(clockFrequency);
    int clock = clockFrequency[currClockFrequency];
    scePowerSetClockFrequency(clock, clock, clock / 2);
}

void textFPS(char* buf, int size)
{
    snprintf(buf, size, "Show FPS: %s", showFPS ? "On" : "Off");
}

void nextFPS(void)
{
    showFPS = !showFPS;
}

void textPalette(char* buf, int size)
{
    snprintf(buf, size, "Palette: %d", currPalette);
}

void prevPalette(void)
{
    currPalette--;
    if (currPalette < 0) currPalette = SV_COLOR_SCHEME_COUNT - 1;
    supervision_set_color_scheme(currPalette);
}

void nextPalette(void)
{
    currPalette = (currPalette + 1) % SV_COLOR_SCHEME_COUNT;
    supervision_set_color_scheme(currPalette);
}

void textMute(char* buf, int size)
{
    snprintf(buf, size, "Audio: %s", !muteAudio ? "On" : "Off");
}

void nextMute(void)
{
    muteAudio = !muteAudio;
}

void textVSync(char* buf, int size)
{
    snprintf(buf, size, "VSync: %s", enabledVSync ? "On" : "Off");
}

void nextVSync(void)
{
    enabledVSync = !enabledVSync;
}

// Graphics

#define SLICE_SIZE 64
#define Viewport_X         0
#define Viewport_Y         0
#define Viewport_Width  SV_W
#define Viewport_Height SV_H

void Blit(int dx, int dy, int dw, int dh)
{
    sceGuScissor(dx, dy, dx + dw, dy + dh);

    struct VertexF* vertices;
    float start, end, slsz_scaled;
    slsz_scaled = ceil((float)dw * SLICE_SIZE / Viewport_Width);

    for (start = Viewport_X, end = Viewport_X + Viewport_Width; start < end; start += SLICE_SIZE, dx += slsz_scaled) {
        vertices = (struct VertexF*)sceGuGetMemory(2 * sizeof(struct VertexF));

        vertices[0].u = start;              vertices[0].v = Viewport_Y;
        vertices[1].u = start + SLICE_SIZE; vertices[1].v = Viewport_Y + Viewport_Height;

        vertices[0].x = dx - 0.5f;               vertices[0].y = dy - 0.5f;
        vertices[1].x = dx - 0.5f + slsz_scaled; vertices[1].y = dy + dh + 0.5f;

        vertices[0].z = vertices[1].z = 0;

        sceGuDrawArray(GU_SPRITES,
            GU_TEXTURE_32BITF | GU_VERTEX_32BITF | GU_TRANSFORM_2D, 2, 0, vertices);
    }

    sceGuScissor(0, 0, SCR_WIDTH, SCR_HEIGHT);
}

void DrawButton(int button, int x, int y)
{
    static uint32_t __attribute__((aligned(16))) buttonTrian[16 * 16];
    static uint32_t __attribute__((aligned(16))) buttonCircl[16 * 16];
    static uint32_t __attribute__((aligned(16))) buttonCross[16 * 16];
    static uint32_t __attribute__((aligned(16))) buttonSquar[16 * 16];
    static BOOL loaded = FALSE;

    uint32_t* pixs[] = { buttonTrian, buttonCircl, buttonCross, buttonSquar };

    if (!loaded) {
        loaded = TRUE;
        const int widths[] = { 4, 8, 10, 12, 14, 14, 16, 16 };
        int x, y, z;
        for (z = 0; z < 4; z++) {
            memset(pixs[z], 0x7F, sizeof(buttonCross)); // GRAY, menu background
            for (y = 0; y < 8; y++) {
                int w = widths[y];
                for (x = 0; x < w; x++) {
                    pixs[z][16 *        y + x + ((16 - w)>>1)] = 0xFF484848;
                    pixs[z][16 * (15 - y) + x + ((16 - w)>>1)] = 0xFF484848;
                }
            }
        }
        // /\ . 0xFF9DBF1B
        for (x = 0; x < 10; x++) {
            buttonTrian[16 * 11 + (x + 3)] = 0xFF9DBF1B; // _
            buttonTrian[16 * (11 - x) + (x / 2 + 3) ] = 0xFF9DBF1B; // /
            buttonTrian[16 * (11 - x) + (12 - x / 2)] = 0xFF9DBF1B; // \ .
        }

        // O
        for (y = 0; y < 32; y++) {
            buttonCircl[16 * (int)(sin(M_PI / 16 * y + M_PI / 32) * 5.5 + 8)
                           + (int)(cos(M_PI / 16 * y + M_PI / 32) * 5.5 + 8)] = 0xFF0D4FF6;
        }
        // X
        for (y = 0; y < 10; y++) {
            buttonCross[16 * (y + 3) +        y + 3] = 0xFFD8ABA3; // \ .
            buttonCross[16 * (y + 3) + (15 - y) - 3] = 0xFFD8ABA3; // /
        }
        // []
        for (y = 0; y < 10; y++) {
            buttonSquar[16 * (y + 3) + 3]        = 0xFFBF92D6;
            buttonSquar[16 * 3 + (y + 3)]        = 0xFFBF92D6;
            buttonSquar[16 * (y + 3) + (15 - 3)] = 0xFFBF92D6;
            buttonSquar[16 * (15 - 3) + (y + 3)] = 0xFFBF92D6;
        }
    }

    switch (button) {
    case PSP_CTRL_TRIANGLE:
        button = 0;
        break;
    case PSP_CTRL_CIRCLE:
        button = 1;
        break;
    case PSP_CTRL_CROSS:
        button = 2;
        break;
    case PSP_CTRL_SQUARE:
        button = 3;
        break;
    }
    sceGuCopyImage(PIX_FORMAT, 0, 0, 16, 16, 16, pixs[button],
        x, y, BUF_WIDTH, (void*)(((unsigned int)fbpc) + 0x4000000));
}
