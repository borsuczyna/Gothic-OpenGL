#include "gl_window.h"
#include <GL/gl.h>
#include <cstdio>

typedef BOOL (APIENTRY *PFNWGLSWAPINTERVALEXTPROC)(int interval);
static PFNWGLSWAPINTERVALEXTPROC wglSwapIntervalEXT = nullptr;

static HWND g_gothicHwnd = nullptr;
static WNDPROC g_origGothicWndProc = nullptr;
static HWND g_hwnd = nullptr;
static HDC g_hdc = nullptr;
static HGLRC g_hglrc = nullptr;
static bool g_initialized = false;
static volatile bool g_ready = false;
static volatile bool g_running = false;
static bool g_swapIntervalSet = false;
static HANDLE g_thread = nullptr;
static HANDLE g_readyEvent = nullptr;

static int g_gameWidth = 800;
static int g_gameHeight = 600;

static const char* WND_CLASS = "GOpenGL_WND";

static LRESULT CALLBACK GothicSubclassProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_SHOWWINDOW:
        if (wp == TRUE) return 0;
        break;
    case WM_WINDOWPOSCHANGING: {
        WINDOWPOS* pos = (WINDOWPOS*)lp;
        pos->flags &= ~SWP_SHOWWINDOW;
        pos->flags |= SWP_NOZORDER | SWP_NOACTIVATE;
        break;
    }
    case WM_STYLECHANGING:
    case WM_DISPLAYCHANGE:
        return 0;
    case WM_NCACTIVATE:
        return CallWindowProcA(g_origGothicWndProc, hwnd, msg, FALSE, lp);
    }
    return CallWindowProcA(g_origGothicWndProc, hwnd, msg, wp, lp);
}

static void EnableVisualStyles() {
    static const char manifest[] =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\r\n"
        "<assembly xmlns=\"urn:schemas-microsoft-com:asm.v1\" manifestVersion=\"1.0\">\r\n"
        "  <dependency><dependentAssembly>\r\n"
        "    <assemblyIdentity type=\"win32\" name=\"Microsoft.Windows.Common-Controls\"\r\n"
        "      version=\"6.0.0.0\" processorArchitecture=\"x86\"\r\n"
        "      publicKeyToken=\"6595b64144ccf1df\" language=\"*\"/>\r\n"
        "  </dependentAssembly></dependency>\r\n"
        "</assembly>\r\n";

    char tmpPath[MAX_PATH], tmpFile[MAX_PATH];
    GetTempPathA(MAX_PATH, tmpPath);
    GetTempFileNameA(tmpPath, "gogl", 0, tmpFile);

    HANDLE hFile = CreateFileA(tmpFile, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (hFile != INVALID_HANDLE_VALUE) {
        DWORD written;
        WriteFile(hFile, manifest, sizeof(manifest) - 1, &written, nullptr);
        CloseHandle(hFile);

        ACTCTXA ctx = {};
        ctx.cbSize = sizeof(ctx);
        ctx.lpSource = tmpFile;
        HANDLE hCtx = CreateActCtxA(&ctx);
        if (hCtx != INVALID_HANDLE_VALUE) {
            ULONG_PTR cookie = 0;
            ActivateActCtx(hCtx, &cookie);
        }
        DeleteFileA(tmpFile);
    }
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CLOSE:
        g_running = false;
        PostQuitMessage(0);
        return 0;
    case WM_ERASEBKGND:
        return 1;

    case WM_KEYDOWN:
    case WM_KEYUP:
    case WM_CHAR:
        if (g_gothicHwnd) PostMessageA(g_gothicHwnd, msg, wp, lp);
        return 0;

    case WM_MOUSEMOVE:
    case WM_LBUTTONDOWN: case WM_LBUTTONUP:
    case WM_RBUTTONDOWN: case WM_RBUTTONUP:
    case WM_MBUTTONDOWN: case WM_MBUTTONUP:
    case WM_MOUSEWHEEL:
        if (g_gothicHwnd) PostMessageA(g_gothicHwnd, msg, wp, lp);
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

static DWORD WINAPI GLThreadProc(LPVOID) {
    EnableVisualStyles();

    HINSTANCE hInst = GetModuleHandleA(nullptr);

    WNDCLASSEXA wc = {};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = WND_CLASS;
    RegisterClassExA(&wc);

    RECT r = {0, 0, 800, 600};
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);

    g_hwnd = CreateWindowExA(
        0, WND_CLASS, "GOpenGL - Gothic OpenGL",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT,
        r.right - r.left, r.bottom - r.top,
        nullptr, nullptr, hInst, nullptr);

    if (!g_hwnd) {
        printf("[GOpenGL] ERROR: Failed to create GL window\n");
        fflush(stdout);
        SetEvent(g_readyEvent);
        return 1;
    }

    g_hdc = GetDC(g_hwnd);

    PIXELFORMATDESCRIPTOR pfd = {};
    pfd.nSize      = sizeof(pfd);
    pfd.nVersion   = 1;
    pfd.dwFlags    = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    pfd.iLayerType = PFD_MAIN_PLANE;

    int fmt = ChoosePixelFormat(g_hdc, &pfd);
    SetPixelFormat(g_hdc, fmt, &pfd);

    g_hglrc = wglCreateContext(g_hdc);
    wglMakeCurrent(g_hdc, g_hglrc);

    printf("[GOpenGL] OpenGL context created: %s\n", (const char*)glGetString(GL_RENDERER));
    fflush(stdout);

    wglSwapIntervalEXT = (PFNWGLSWAPINTERVALEXTPROC)wglGetProcAddress("wglSwapIntervalEXT");
    if (wglSwapIntervalEXT) {
        wglSwapIntervalEXT(0);
        printf("[GOpenGL] VSync disabled (swap interval = 0)\n");
        fflush(stdout);
    }

    wglMakeCurrent(nullptr, nullptr);

    g_running = true;
    g_ready = true;
    SetEvent(g_readyEvent);

    while (g_running) {
        MSG msg;
        while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                g_running = false;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        Sleep(1);
    }

    HINSTANCE hI = GetModuleHandleA(nullptr);
    if (g_hwnd) { DestroyWindow(g_hwnd); g_hwnd = nullptr; }
    UnregisterClassA(WND_CLASS, hI);
    return 0;
}

void GOpenGL_OnSetWindow(HWND hwnd) {
    if (g_initialized || !hwnd)
        return;
    g_initialized = true;
    g_gothicHwnd = hwnd;

    printf("[GOpenGL] Captured Gothic HWND=0x%p, hiding it\n", hwnd);
    fflush(stdout);

    g_origGothicWndProc = (WNDPROC)SetWindowLongPtrA(hwnd, GWLP_WNDPROC, (LONG_PTR)GothicSubclassProc);
    ShowWindow(hwnd, SW_HIDE);

    g_readyEvent = CreateEventA(nullptr, TRUE, FALSE, nullptr);
    g_thread = CreateThread(nullptr, 0, GLThreadProc, nullptr, 0, nullptr);

    WaitForSingleObject(g_readyEvent, 5000);
    CloseHandle(g_readyEvent);
    g_readyEvent = nullptr;

    if (g_ready) {
        printf("[GOpenGL] Window thread ready, context available for game thread\n");
        fflush(stdout);
    }
}

bool GOpenGL_AcquireContext() {
    if (!g_ready || !g_hdc || !g_hglrc) return false;
    if (wglMakeCurrent(g_hdc, g_hglrc) != TRUE) return false;

    if (!g_swapIntervalSet) {
        g_swapIntervalSet = true;
        if (!wglSwapIntervalEXT)
            wglSwapIntervalEXT = (PFNWGLSWAPINTERVALEXTPROC)wglGetProcAddress("wglSwapIntervalEXT");
        if (wglSwapIntervalEXT) {
            wglSwapIntervalEXT(0);
            printf("[GOpenGL] VSync disabled from game thread (swap interval = 0)\n");
            fflush(stdout);
        }
    }
    return true;
}

void GOpenGL_OnPresent() {
    if (!g_initialized || !g_hdc || !g_hwnd) return;
    SwapBuffers(g_hdc);
}

void GOpenGL_StopOpenGL() {
    if (g_running) {
        g_running = false;
        if (g_hglrc) {
            wglMakeCurrent(nullptr, nullptr);
        }
        if (g_thread) {
            PostThreadMessageA(GetThreadId(g_thread), WM_QUIT, 0, 0);
            WaitForSingleObject(g_thread, 3000);
            CloseHandle(g_thread);
            g_thread = nullptr;
        }
        if (g_hglrc) {
            wglDeleteContext(g_hglrc);
            g_hglrc = nullptr;
        }
        if (g_hdc && g_hwnd) {
            ReleaseDC(g_hwnd, g_hdc);
            g_hdc = nullptr;
        }
    }
    g_initialized = false;
    g_ready = false;
}

void GOpenGL_SetGameResolution(int w, int h) {
    g_gameWidth = w;
    g_gameHeight = h;
    printf("[GOpenGL] Game resolution set to %dx%d\n", w, h);
    fflush(stdout);
}

HDC GOpenGL_GetDC() { return g_hdc; }
HWND GOpenGL_GetHWND() { return g_hwnd; }

int GOpenGL_GetWindowWidth() {
    if (!g_hwnd) return 800;
    RECT rc;
    GetClientRect(g_hwnd, &rc);
    return rc.right - rc.left;
}

int GOpenGL_GetWindowHeight() {
    if (!g_hwnd) return 600;
    RECT rc;
    GetClientRect(g_hwnd, &rc);
    return rc.bottom - rc.top;
}

int GOpenGL_GetGameWidth()  { return g_gameWidth; }
int GOpenGL_GetGameHeight() { return g_gameHeight; }
