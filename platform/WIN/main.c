#pragma comment(linker,"\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

#include <windows.h>
//#include <commctrl.h>
//#pragma comment(lib, "Comctl32.lib")
#include <stdlib.h>
#include <stdio.h>
#include <tchar.h>

// PathFindFileName()
#include <shlwapi.h>
#pragma comment(lib, "Shlwapi.lib")

#define TERRIBLE_AUDIO_IMPLEMENTATION

#ifdef TERRIBLE_AUDIO_IMPLEMENTATION
#include <mmsystem.h>
#pragma comment(lib, "Winmm.lib")
#endif

#include "resource.h"
#include "../../common/supervision.h"

#ifdef TERRIBLE_AUDIO_IMPLEMENTATION
WAVEHDR whdr;
HWAVEOUT hWaveOut;
// 44100 / FPS * 2 (channels) * 2 (16-bit)
#define BUFFER_SIZE 2940
INT8 audioBuffer[BUFFER_SIZE];
#endif

volatile BOOL finished = FALSE;
volatile BOOL execute = FALSE;

LPCTSTR szClassName = _T("Potator (WinAPI)");
LPCTSTR VERSION = _T("1.0.1");
#define WINDOW_STYLE (WS_SYSMENU | WS_MINIMIZEBOX | WS_CAPTION | WS_VISIBLE)
#define WINDOW_EX_STYLE (WS_EX_CLIENTEDGE)

HWND hWindow;
HMENU hMenu;
HDC hDC;

DWORD threadID;
HANDLE runthread = INVALID_HANDLE_VALUE;

char romName[MAX_PATH];
UINT8 *buffer;
UINT32 bufferSize = 0;
UINT8 windowScale = 2;
UINT16 screenBuffer[SV_W * SV_H];

int keysMapping[] = {
      VK_RIGHT
    , VK_LEFT
    , VK_DOWN
    , VK_UP
    , 'X'
    , 'C'
    , 'Z'
    , VK_SPACE
};
LPCTSTR keysNames[] = {
      _T("Right")
    , _T("Left")
    , _T("Down")
    , _T("Up")
    , _T("B")
    , _T("A")
    , _T("Select")
    , _T("Start")
};

#define UPDATE_RATE 60
UINT64 startCounter;
UINT64 freq;

void InitCounter(void)
{
    QueryPerformanceFrequency((LARGE_INTEGER*)&freq);
    QueryPerformanceCounter((LARGE_INTEGER*)&startCounter);
}

BOOL NeedUpdate(void)
{
    static double elapsedCounter = 0.0;
    static BOOL result = FALSE;

    // New frame
    if (!result) {
        UINT64 now;
        QueryPerformanceCounter((LARGE_INTEGER*)&now);
        elapsedCounter += (double)((now - startCounter) * 1000) / freq;
        startCounter = now;
    }
    result = elapsedCounter >= 1000.0 / UPDATE_RATE;
    if (result) {
        elapsedCounter -= 1000.0 / UPDATE_RATE;
    }
    return result;
}

// FIXME: IPC (lock Load ROM before exit 'while (execute)')
DWORD WINAPI run(LPVOID lpParameter)
{
    BITMAPV4HEADER bmi = { 0 };
    TCHAR txt[64];
    UINT64 curticks = 0;
    UINT64 fpsticks = 0;
    UINT16 fpsframecount = 0;

    HANDLE hTimer;
    LARGE_INTEGER dueTime = { 0 };
    // Reduce CPU usage
    hTimer = CreateWaitableTimer(NULL, FALSE, NULL);
    SetWaitableTimer(hTimer, &dueTime, 1000 / UPDATE_RATE, NULL, NULL, TRUE);

    bmi.bV4Size = sizeof(bmi);
    bmi.bV4Planes = 1;
    bmi.bV4BitCount = 16;
    bmi.bV4V4Compression = BI_RGB | BI_BITFIELDS;
    bmi.bV4RedMask   = 0x001F;
    bmi.bV4GreenMask = 0x03E0;
    bmi.bV4BlueMask  = 0x7C00;
    bmi.bV4Width  =  SV_W;
    bmi.bV4Height = -SV_H;

    while (!finished) {
        InitCounter();
        while (execute) {
            UINT8 controls_state = 0;

            for (int i = 0; i < 8; i++) {
                if (GetAsyncKeyState(keysMapping[i]) & 0x8000)
                    controls_state |= 1 << i;
            }

            supervision_set_input(controls_state);

            while (NeedUpdate()) {
                supervision_exec(screenBuffer, FALSE);

#ifdef TERRIBLE_AUDIO_IMPLEMENTATION
                supervision_update_sound(audioBuffer, BUFFER_SIZE / 2);
                INT16* dst = (INT16*)audioBuffer;
                UINT8* src = (UINT8*)audioBuffer;
                for (int i = BUFFER_SIZE / 2 - 1; i >= 0; i--) {
                    dst[i] = src[i] << (8 + 1);
                }
                waveOutPrepareHeader(hWaveOut, &whdr, sizeof(whdr));
                waveOutWrite(hWaveOut, &whdr, sizeof(whdr));
                whdr.dwFlags = 0;
#endif

                fpsframecount++;
                QueryPerformanceCounter((LARGE_INTEGER *)&curticks);

                if (fpsticks + freq < curticks)
                    fpsticks = curticks; // Initial value
                if (curticks >= fpsticks) {
                    _sntprintf_s(txt, _countof(txt), _TRUNCATE , _T("%s [FPS: %d]"), szClassName, fpsframecount);
                    SetWindowText(hWindow, txt);
                    fpsframecount = 0;
                    fpsticks += freq;
                }

                RECT r;
                GetClientRect(hWindow, &r);
                LONG x = 0, w = r.right - r.left;
                LONG y = 0, h = r.bottom - r.top;
                if (w != h) {
                    // Center
                    LONG size = h / SV_H * SV_H;
                    x = (w - size) / 2; w = size;
                    y = (h - size) / 2; h = size;
                }
                StretchDIBits(hDC, x, y, w, h, 0, 0, SV_W, SV_H, screenBuffer, (BITMAPINFO*)&bmi, DIB_RGB_COLORS, SRCCOPY);
            }

            WaitForSingleObject(hTimer, INFINITE);
        }
        execute = FALSE;
        WaitForSingleObject(hTimer, INFINITE);
    }
    return 1;
}

void SetRomName(LPCTSTR path)
{
#ifdef UNICODE
    WideCharToMultiByte(CP_UTF8, 0, PathFindFileName(path), -1, romName, MAX_PATH, NULL, NULL);
#else
    strcpy_s(romName, sizeof(romName), PathFindFileName(path));
#endif
}

int LoadROM(LPCTSTR fileName)
{
    if (buffer != NULL) {
        free(buffer);
        buffer = NULL;
    }
    DWORD dwTemp;
    HANDLE hFile = CreateFile(fileName, GENERIC_READ, 0, NULL,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (INVALID_HANDLE_VALUE == hFile) {
        return 1;
    }
    bufferSize = GetFileSize(hFile, NULL);
    buffer = (UINT8 *)malloc(bufferSize);
    ReadFile(hFile, buffer, bufferSize, &dwTemp, NULL);
    if (bufferSize != dwTemp) {
        CloseHandle(hFile);
        return 1;
    }
    CloseHandle(hFile);
    SetRomName(fileName);
    return 0;
}

void UpdateGhosting(void);
void UpdatePalette(void);

void LoadBuffer(void)
{
    supervision_load(buffer, bufferSize);
    UpdateGhosting();
    UpdatePalette();
}

void StopEmulation(void)
{
    execute = FALSE;
    Sleep(1000 / UPDATE_RATE * 4); // Wait for the end of execution
}

void ResumeEmulation(void)
{
    execute = TRUE;
}

void UpdateWindowSize(void)
{
    CheckMenuRadioItem(hMenu, IDM_SIZE1, IDM_SIZE6, IDM_SIZE1 + windowScale - 1, MF_BYCOMMAND);
    RECT r;
    SetRect(&r, 0, 0, SV_W * windowScale, SV_H * windowScale);
    AdjustWindowRectEx(&r, WINDOW_STYLE, TRUE, WINDOW_EX_STYLE);
    SetWindowPos(hWindow, NULL, 0, 0, r.right - r.left, r.bottom - r.top, SWP_NOMOVE | SWP_NOZORDER | SWP_SHOWWINDOW);
}

int currGhosting = 0;

void UpdateGhosting(void)
{
    CheckMenuRadioItem(hMenu, IDM_GHOSTING, IDM_GHOSTING + SV_GHOSTING_MAX, IDM_GHOSTING + currGhosting, MF_BYCOMMAND);
    supervision_set_ghosting(currGhosting);
}

int currPalette = 0;

void UpdatePalette(void)
{
    CheckMenuRadioItem(hMenu, IDM_PALETTE, IDM_PALETTE + SV_COLOR_SCHEME_COUNT - 1, IDM_PALETTE + currPalette, MF_BYCOMMAND);
    supervision_set_color_scheme(currPalette);
}

void InitMenu(void)
{
    HMENU hMenuGhosting = CreateMenu();
    for (int i = 0; i < SV_GHOSTING_MAX + 1; i++) {
        TCHAR buf[16];
        if (i == 0) {
            _sntprintf_s(buf, _countof(buf), _TRUNCATE, _T("Off"));
        }
        else {
            _sntprintf_s(buf, _countof(buf), _TRUNCATE, _T("%d frame%s"), i, (i > 1 ? _T("s") : _T("")));
        }
        AppendMenu(hMenuGhosting, MF_STRING, IDM_GHOSTING + i, buf);
    }
    HMENU hMenuOptions = GetSubMenu(hMenu, 1);
    InsertMenu(hMenuOptions, 2, MF_POPUP | MF_BYPOSITION, (UINT_PTR)hMenuGhosting, _T("Ghosting"));
    UpdateGhosting();

    HMENU hMenuPalette = CreateMenu();
    for (int i = 0; i < SV_COLOR_SCHEME_COUNT; i++) {
        TCHAR buf[2];
        _sntprintf_s(buf, _countof(buf), _TRUNCATE, _T("%d"), i);
        AppendMenu(hMenuPalette, MF_STRING, IDM_PALETTE + i, buf);
    }
    InsertMenu(hMenuOptions, 3, MF_POPUP | MF_BYPOSITION, (UINT_PTR)hMenuPalette, _T("Palette"));
    UpdatePalette();
}

BOOL showCursorFullscreen = FALSE;

BOOL IsFullscreen(void)
{
    return GetWindowLongPtr(hWindow, GWL_STYLE) & WS_POPUP;
}

void ToggleFullscreen(void)
{
    static WINDOWPLACEMENT wp;
    if (IsFullscreen()) {
        SetWindowLongPtr(hWindow, GWL_STYLE, WINDOW_STYLE);
        SetWindowLongPtr(hWindow, GWL_EXSTYLE, WINDOW_EX_STYLE);
        SetWindowPlacement(hWindow, &wp);
        SetWindowPos(hWindow, NULL, 0, 0, 0, 0,
            SWP_FRAMECHANGED | SWP_NOOWNERZORDER | SWP_NOZORDER | SWP_NOMOVE | SWP_NOSIZE);
        SetMenu(hWindow, hMenu);
        if (!showCursorFullscreen)
            ShowCursor(TRUE);
    }
    else {
        showCursorFullscreen = FALSE;
        wp.length = sizeof(wp);
        GetWindowPlacement(hWindow, &wp);
        SetWindowLongPtr(hWindow, GWL_STYLE, WS_VISIBLE | WS_POPUP);
        SetWindowLongPtr(hWindow, GWL_EXSTYLE, 0);
        int w = GetSystemMetrics(SM_CXSCREEN);
        int h = GetSystemMetrics(SM_CYSCREEN);
        // SWP_NOOWNERZORDER - prevents showing child windows on top
        SetWindowPos(hWindow, HWND_TOP, 0, 0, w, h, SWP_FRAMECHANGED | SWP_NOOWNERZORDER);
        SetMenu(hWindow, NULL);
        ShowCursor(FALSE);
        RECT r = { 0, 0, w, h };
        FillRect(hDC, &r, (HBRUSH)GetStockObject(BLACK_BRUSH));
    }
    InvalidateRect(hWindow, NULL, TRUE);
}

void RegisterControlConfigClass(HWND);
void CreateControlConfigWindow(HWND);

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    static HFONT hFont;
    switch (msg) {
    case WM_CREATE:
        hFont = CreateFont(0, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, _T("Arial"));
        RegisterControlConfigClass(hWnd);
        break;
    case WM_COMMAND:
    {
        int wmId = LOWORD(wParam);
        if (wmId >= IDM_GHOSTING && wmId <= IDM_GHOSTING + SV_GHOSTING_MAX) {
            currGhosting = wmId - IDM_GHOSTING;
            UpdateGhosting();
            break;
        }
        else if (wmId >= IDM_PALETTE && wmId <= IDM_PALETTE + SV_COLOR_SCHEME_COUNT - 1) {
            currPalette = wmId - IDM_PALETTE;
            UpdatePalette();
            break;
        }
        switch (wmId) {
        case IDM_OPEN:
            {
                execute = FALSE; // Stop emulation while opening new rom
                TCHAR szFileName[MAX_PATH] = _T("");
                OPENFILENAME ofn;
                ZeroMemory(&ofn, sizeof(ofn));
                ofn.lStructSize = sizeof(ofn);
                ofn.hwndOwner = hWnd;
                ofn.lpstrFilter = _T("Supervision files (.sv, .ws, .bin)\0*.sv;*.ws;*.bin\0\0");
                ofn.nFilterIndex = 1;
                ofn.lpstrFile = szFileName;
                ofn.nMaxFile = MAX_PATH;
                ofn.lpstrDefExt = _T("sv");
                ofn.Flags = OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

                if (!GetOpenFileName(&ofn)) {
                    if (buffer)
                        execute = TRUE;
                    return 0;
                }

                if (LoadROM(szFileName) == 0) {
                    LoadBuffer();
                    execute = TRUE;
                }
            }
            break;
        case IDM_RESET:
            LoadBuffer();
            break;
        case IDM_SAVE:
            if (buffer) {
                StopEmulation();
                supervision_save_state(romName, 0);
                ResumeEmulation();
            }
            break;
        case IDM_LOAD:
            if (buffer) {
                StopEmulation();
                supervision_load_state(romName, 0);
                ResumeEmulation();
            }
            break;
        case IDM_WEBSITE:
            ShellExecute(NULL, _T("open"), _T("https://github.com/infval/potator"), NULL, NULL, SW_SHOWNORMAL);
            break;
        case IDM_ABOUT:
        {
            TCHAR text[64] = { 0 };
            _sntprintf_s(text, _countof(text), _TRUNCATE, _T("%s %s (core: %u.%u.%u)"),
                szClassName, VERSION, SV_CORE_VERSION_MAJOR, SV_CORE_VERSION_MINOR, SV_CORE_VERSION_PATCH);
            MessageBox(NULL, text, _T("About"), MB_ICONEXCLAMATION | MB_OK);
        }
            break;
        case IDM_FULLSCREEN:
            ToggleFullscreen();
            break;
        case IDM_SIZE1:
        case IDM_SIZE2:
        case IDM_SIZE3:
        case IDM_SIZE4:
        case IDM_SIZE5:
        case IDM_SIZE6:
            windowScale = wmId - IDM_SIZE1 + 1;
            UpdateWindowSize();
            break;
        case IDM_CONTROL_CONFIG:
            CreateControlConfigWindow(hWnd);
            break;
        case IDM_EXIT:
            DestroyWindow(hWnd);
            break;
        default:
            return DefWindowProc(hWnd, msg, wParam, lParam);
        }
    }
        break;
    case WM_DROPFILES:
    {
        StopEmulation();
        HDROP hDrop = (HDROP)wParam;
        TCHAR szFileName[MAX_PATH] = _T("");
        DragQueryFile(hDrop, 0, szFileName, MAX_PATH);
        DragFinish(hDrop);

        if (LoadROM(szFileName) == 0) {
            LoadBuffer();
            ResumeEmulation();
        }
    }
        break;
    case WM_LBUTTONDBLCLK:
        ToggleFullscreen();
        break;
    case WM_SYSCOMMAND:
        // Alt or F10 show menu
        if (wParam == SC_KEYMENU && IsFullscreen()) {
            showCursorFullscreen = !showCursorFullscreen;
            ShowCursor(showCursorFullscreen);
            SetMenu(hWindow, showCursorFullscreen ? hMenu : NULL);
            return DefWindowProc(hWnd, msg, wParam, lParam); // If break;, menu doesn't get focus
        }
        return DefWindowProc(hWnd, msg, wParam, lParam);
    case WM_PAINT:
    {
        if (!buffer && !IsFullscreen()) {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);
            SelectObject(hdc, hFont);
            TEXTMETRIC tm;
            GetTextMetrics(hdc, &tm);
            SetTextColor(hdc, RGB(255, 255, 255));
            SetBkColor(hdc, RGB(0, 0, 0));
#define X(s) _T(s), _countof(s)
            TextOut(hdc, 8, 8                  , X("Open ROM:"));
            TextOut(hdc, 8, 8 + tm.tmHeight * 1, X("Drag & Drop"));
            TextOut(hdc, 8, 8 + tm.tmHeight * 3, X("Toggle Fullscreen:"));
            TextOut(hdc, 8, 8 + tm.tmHeight * 4, X("Double-Click or Alt+Enter"));
            TextOut(hdc, 8, 8 + tm.tmHeight * 6, X("Toggle Menu in Fullscreen:"));
            TextOut(hdc, 8, 8 + tm.tmHeight * 7, X("Press Alt or F10"));
#undef X
            EndPaint(hWnd, &ps);
        }
    }
        break;
    case WM_CLOSE:
        DeleteObject(hFont);
        DestroyWindow(hWnd);
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hWnd, msg, wParam, lParam);
    }
    return 0;
}

int APIENTRY _tWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPTSTR lpCmdLine, int nCmdShow)
{
    //INITCOMMONCONTROLSEX ccs = { 0 };
    //ccs.dwSize = sizeof(ccs);
    //ccs.dwICC = ICC_STANDARD_CLASSES;
    //InitCommonControlsEx(&ccs);

    supervision_init();

    WNDCLASSEX wcex;
    wcex.cbSize        = sizeof(WNDCLASSEX);
    wcex.hInstance     = hInstance;
    wcex.lpszClassName = szClassName;
    wcex.lpfnWndProc   = WndProc;
    wcex.style         = CS_DBLCLKS;
    wcex.hIcon         = LoadIcon(NULL, IDI_APPLICATION);
    wcex.hIconSm       = LoadIcon(NULL, IDI_APPLICATION);
    wcex.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wcex.lpszMenuName  = _T("MENU_PRINCIPAL");
    wcex.cbClsExtra    = 0;
    wcex.cbWndExtra    = 0;
    wcex.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH); // (HBRUSH)COLOR_BACKGROUND;

    if (!RegisterClassEx(&wcex)) {
        MessageBox(NULL, _T("Window registration failed!"), szClassName, MB_ICONEXCLAMATION | MB_OK);
        return FALSE;
    }

    hWindow = CreateWindowEx(WINDOW_EX_STYLE, szClassName, szClassName, WINDOW_STYLE,
        CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, NULL, NULL, hInstance, NULL);
    if (!hWindow) {
        MessageBox(NULL, _T("Window creation failed!"), szClassName, MB_ICONEXCLAMATION | MB_OK);
        return FALSE;
    }

    DragAcceptFiles(hWindow, TRUE);

    hDC = GetDC(hWindow);
    hMenu = GetMenu(hWindow);
    UpdateWindowSize();

    InitMenu();

    ShowWindow(hWindow, nCmdShow);
    UpdateWindow(hWindow);

#ifdef TERRIBLE_AUDIO_IMPLEMENTATION
    whdr.lpData = (LPSTR)audioBuffer;
    whdr.dwBufferLength = BUFFER_SIZE;
    whdr.dwFlags = 0;
    whdr.dwLoops = 0;
    WAVEFORMATEX wfx;
    wfx.nSamplesPerSec = SV_SAMPLE_RATE;
    wfx.wBitsPerSample = 16;
    wfx.nChannels = 2;
    wfx.cbSize = 0;
    wfx.wFormatTag = WAVE_FORMAT_PCM;
    wfx.nBlockAlign = (wfx.wBitsPerSample * wfx.nChannels) / 8;
    wfx.nAvgBytesPerSec = wfx.nBlockAlign * wfx.nSamplesPerSec;
    if (waveOutOpen(&hWaveOut, WAVE_MAPPER, &wfx, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
        return FALSE;
    }
#endif

    runthread = CreateThread(NULL, 0, run, NULL, 0, &threadID);

    HACCEL hAccelTable = LoadAccelerators(hInstance, _T("IDR_MAIN_ACCEL"));

    MSG msg;

    while (GetMessage(&msg, NULL, 0, 0))
    {
        if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    ReleaseDC(hWindow, hDC);

    UnregisterClass(wcex.lpszClassName, hInstance);

    return (int)msg.wParam;
}

// Control Config

WPARAM MapLeftRightKeys(WPARAM vk, LPARAM lParam);
void VirtualKeyCodeToString(UINT virtualKey, LPTSTR szName, int size);

HWND hButton;
int buttonId;

LRESULT CALLBACK GetMsgProc(int code, WPARAM wParam, LPARAM lParam)
{
    if (code < 0) {
        return CallNextHookEx(NULL, code, wParam, lParam);
    }
    MSG msg = *((MSG*)lParam);
    if (msg.message == WM_KEYDOWN) {
        if (hButton) {
            TCHAR buf[32] = { 0 };
            GetKeyNameText(msg.lParam, buf, _countof(buf));
            SetWindowText(hButton, buf);
            EnableWindow(hButton, TRUE);
            hButton = NULL;
            keysMapping[buttonId - 1] = (int)MapLeftRightKeys(msg.wParam, msg.lParam);
        }
    }
    return CallNextHookEx(NULL, code, wParam, lParam);
}

HWND hControlConfigWindow;

LRESULT CALLBACK ControlConfigProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    static HHOOK hHook;
    static HFONT hFont;
    switch (msg) {
    case WM_CREATE:
        hButton = NULL;
        buttonId = 0;
        hFont = CreateFont(0, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, _T("Arial"));
        for (int i = 0; i < 8; i++) {
            HWND h = CreateWindow(_T("static"), keysNames[i], WS_VISIBLE | WS_CHILD,
                11, 11 + i * 30, 80, 25, hwnd, (HMENU)((UINT_PTR)i + 1 + 8), NULL, NULL);
            SendMessage(h, WM_SETFONT, (WPARAM)hFont, TRUE);
            TCHAR name[32];
            VirtualKeyCodeToString(keysMapping[i], name, _countof(name));
            h = CreateWindow(_T("button"), name, WS_VISIBLE | WS_CHILD,
                80, 8 + i * 30, 120, 25, hwnd, (HMENU)((UINT_PTR)i + 1), NULL, NULL);
            SendMessage(h, WM_SETFONT, (WPARAM)hFont, TRUE);
        }
        hHook = SetWindowsHookEx(WH_GETMESSAGE, GetMsgProc, GetModuleHandle(NULL), GetCurrentThreadId());
        break;
    case WM_COMMAND:
        {
            int wmId = LOWORD(wParam);
            if (wmId >= 1 && wmId <= 8) {
                if (hButton) break;
                hButton = (HWND)lParam;
                buttonId = wmId;
                EnableWindow(hButton, FALSE);
                SetWindowText(hButton, _T("Press Key"));
                SetFocus(hwnd);
            }
        }
        break;
    case WM_CLOSE:
        hControlConfigWindow = NULL;
        DeleteObject(hFont);
        UnhookWindowsHookEx(hHook);
        DestroyWindow(hwnd);
        break;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

void RegisterControlConfigClass(HWND hwnd)
{
    WNDCLASSEX wc = { 0 };
    wc.cbSize        = sizeof(WNDCLASSEX);
    wc.hInstance     = GetModuleHandle(NULL);
    wc.lpszClassName = _T("ControlConfigClass");
    wc.lpfnWndProc   = ControlConfigProc;
    wc.hbrBackground = GetSysColorBrush(COLOR_3DFACE);
    RegisterClassEx(&wc);
}

void CreateControlConfigWindow(HWND hwnd)
{
    if (hControlConfigWindow)
        return;
    RECT r;
    GetWindowRect(hWindow, &r);
    hControlConfigWindow = CreateWindowEx(WS_EX_DLGMODALFRAME, _T("ControlConfigClass"), _T("Control Config"),
        WS_VISIBLE | WS_SYSMENU | WS_CAPTION, r.left + 64, r.top + 64, 228, 290,
        hwnd, NULL, GetModuleHandle(NULL), NULL);
}

// https://stackoverflow.com/a/15977613
WPARAM MapLeftRightKeys(WPARAM virtualKey, LPARAM lParam)
{
    UINT scancode = (lParam & 0x00ff0000) >> 16;
    int  extended = (lParam & 0x01000000) != 0;
    switch (virtualKey) {
    case VK_SHIFT:
        return MapVirtualKey(scancode, MAPVK_VSC_TO_VK_EX);
    case VK_CONTROL:
        return extended ? VK_RCONTROL : VK_LCONTROL;
    case VK_MENU:
        return extended ? VK_RMENU : VK_LMENU;
    }
    return virtualKey;
}

// https://stackoverflow.com/a/38107083
void VirtualKeyCodeToString(UINT virtualKey, LPTSTR szName, int size)
{
    UINT scanCode = MapVirtualKey(virtualKey, MAPVK_VK_TO_VSC);
    switch (virtualKey) {
    case VK_LEFT:     case VK_UP:    case VK_RIGHT: case VK_DOWN:
    case VK_RCONTROL: case VK_RMENU:
    case VK_LWIN:     case VK_RWIN:  case VK_APPS:
    case VK_INSERT:   case VK_DELETE:
    case VK_HOME:     case VK_END:
    case VK_PRIOR:    case VK_NEXT:
    case VK_NUMLOCK:  case VK_DIVIDE:
        scanCode |= KF_EXTENDED;
    }
    GetKeyNameText(scanCode << 16, szName, size);
}
