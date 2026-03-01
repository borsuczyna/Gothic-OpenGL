#include "opengl_window.h"
#include <GL/gl.h>
#include <cstdio>

static HWND g_gothicHwnd = nullptr;
static HWND g_hwnd = nullptr;
static HDC g_hdc = nullptr;
static HGLRC g_hglrc = nullptr;
static bool g_initialized = false;
static bool g_windowCreated = false;
static volatile bool g_running = false;
static HANDLE g_thread = nullptr;

static const char* WND_CLASS = "GVLK_GL";

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
    GetTempFileNameA(tmpPath, "gvlk", 0, tmpFile);

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
        0, WND_CLASS, "GVLK - Gothic OpenGL",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT,
        r.right - r.left, r.bottom - r.top,
        nullptr, nullptr, hInst, nullptr);

    if (!g_hwnd) {
        printf("[GVLK] ERROR: Failed to create GL window\n");
        fflush(stdout);
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

    printf("[GVLK] OpenGL window created: %s\n", (const char*)glGetString(GL_RENDERER));
    fflush(stdout);

    g_running = true;
    g_windowCreated = true;
    float xOffset = 0.0f;
    float direction = 1.0f;

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
        if (!g_running) break;

        RECT rc;
        GetClientRect(g_hwnd, &rc);
        int w = rc.right - rc.left;
        int h = rc.bottom - rc.top;

        if (w > 0 && h > 0) {
            glViewport(0, 0, w, h);
            glMatrixMode(GL_PROJECTION);
            glLoadIdentity();
            float aspect = (float)w / (float)h;
            glOrtho(-aspect, aspect, -1.0, 1.0, -1.0, 1.0);
            glMatrixMode(GL_MODELVIEW);
            glLoadIdentity();

            glClearColor(0.1f, 0.1f, 0.2f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            xOffset += 0.01f * direction;
            if (xOffset > 0.8f) direction = -1.0f;
            if (xOffset < -0.8f) direction = 1.0f;

            glBegin(GL_TRIANGLES);
                glColor3f(1.0f, 0.0f, 0.0f);
                glVertex2f(xOffset + 0.0f,  0.6f);
                glColor3f(0.0f, 1.0f, 0.0f);
                glVertex2f(xOffset - 0.6f, -0.4f);
                glColor3f(0.0f, 0.0f, 1.0f);
                glVertex2f(xOffset + 0.6f, -0.4f);
            glEnd();

            SwapBuffers(g_hdc);
        }

        Sleep(16);
    }

    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(g_hglrc);
    g_hglrc = nullptr;
    ReleaseDC(g_hwnd, g_hdc);
    g_hdc = nullptr;
    DestroyWindow(g_hwnd);
    UnregisterClassA(WND_CLASS, hInst);
    g_hwnd = nullptr;
    return 0;
}

void GVLK_OnSetWindow(HWND hwnd) {
    if (g_initialized || !hwnd)
        return;
    g_initialized = true;
    g_gothicHwnd = hwnd;

    printf("[GVLK] Captured Gothic HWND=0x%p, hiding it\n", hwnd);
    fflush(stdout);

    ShowWindow(hwnd, SW_HIDE);

    g_thread = CreateThread(nullptr, 0, GLThreadProc, nullptr, 0, nullptr);
}

void GVLK_OnPresent() {
    if (!g_initialized) return;

    if (g_gothicHwnd && IsWindowVisible(g_gothicHwnd)) {
        ShowWindow(g_gothicHwnd, SW_HIDE);
    }

    MSG msg;
    while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
}

void GVLK_StopOpenGL() {
    if (g_running) {
        g_running = false;
        if (g_thread) {
            WaitForSingleObject(g_thread, 3000);
            CloseHandle(g_thread);
            g_thread = nullptr;
        }
    }
    g_initialized = false;
    g_windowCreated = false;
}
