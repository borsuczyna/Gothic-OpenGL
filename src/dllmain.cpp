#include <windows.h>
#include <cstdio>
#include "renderer/VkWindow.h"
#include "renderer/VkRenderer.h"
#include "proxy/ProxyDdraw7.h"

struct RealDDraw {
    HMODULE dll;
    FARPROC AcquireDDThreadLock;
    FARPROC CheckFullscreen;
    FARPROC CompleteCreateSysmemSurface;
    FARPROC D3DParseUnknownCommand;
    FARPROC DDGetAttachedSurfaceLcl;
    FARPROC DDInternalLock;
    FARPROC DDInternalUnlock;
    FARPROC DSoundHelp;
    FARPROC DirectDrawCreate;
    FARPROC DirectDrawCreateClipper;
    FARPROC DirectDrawCreateEx;
    FARPROC DirectDrawEnumerateA;
    FARPROC DirectDrawEnumerateExA;
    FARPROC DirectDrawEnumerateExW;
    FARPROC DirectDrawEnumerateW;
    FARPROC DllCanUnloadNow;
    FARPROC DllGetClassObject;
    FARPROC GetDDSurfaceLocal;
    FARPROC GetOLEThunkData;
    FARPROC GetSurfaceFromDC;
    FARPROC RegisterSpecialCase;
    FARPROC ReleaseDDThreadLock;
};

static RealDDraw real_ddraw = {};

static void LoadRealDDraw() {
    char path[MAX_PATH];
    GetSystemDirectoryA(path, MAX_PATH);
    strcat(path, "\\ddraw.dll");

    real_ddraw.dll = LoadLibraryA(path);
    if (!real_ddraw.dll) {
        printf("[GVulkan] WARNING: Could not load real ddraw.dll from %s\n", path);
        return;
    }
    printf("[GVulkan] Loaded real ddraw.dll from %s\n", path);

    real_ddraw.AcquireDDThreadLock         = GetProcAddress(real_ddraw.dll, "AcquireDDThreadLock");
    real_ddraw.CheckFullscreen             = GetProcAddress(real_ddraw.dll, "CheckFullscreen");
    real_ddraw.CompleteCreateSysmemSurface = GetProcAddress(real_ddraw.dll, "CompleteCreateSysmemSurface");
    real_ddraw.D3DParseUnknownCommand      = GetProcAddress(real_ddraw.dll, "D3DParseUnknownCommand");
    real_ddraw.DDGetAttachedSurfaceLcl     = GetProcAddress(real_ddraw.dll, "DDGetAttachedSurfaceLcl");
    real_ddraw.DDInternalLock              = GetProcAddress(real_ddraw.dll, "DDInternalLock");
    real_ddraw.DDInternalUnlock            = GetProcAddress(real_ddraw.dll, "DDInternalUnlock");
    real_ddraw.DSoundHelp                  = GetProcAddress(real_ddraw.dll, "DSoundHelp");
    real_ddraw.DirectDrawCreate            = GetProcAddress(real_ddraw.dll, "DirectDrawCreate");
    real_ddraw.DirectDrawCreateClipper     = GetProcAddress(real_ddraw.dll, "DirectDrawCreateClipper");
    real_ddraw.DirectDrawCreateEx          = GetProcAddress(real_ddraw.dll, "DirectDrawCreateEx");
    real_ddraw.DirectDrawEnumerateA        = GetProcAddress(real_ddraw.dll, "DirectDrawEnumerateA");
    real_ddraw.DirectDrawEnumerateExA      = GetProcAddress(real_ddraw.dll, "DirectDrawEnumerateExA");
    real_ddraw.DirectDrawEnumerateExW      = GetProcAddress(real_ddraw.dll, "DirectDrawEnumerateExW");
    real_ddraw.DirectDrawEnumerateW        = GetProcAddress(real_ddraw.dll, "DirectDrawEnumerateW");
    real_ddraw.DllCanUnloadNow             = GetProcAddress(real_ddraw.dll, "DllCanUnloadNow");
    real_ddraw.DllGetClassObject           = GetProcAddress(real_ddraw.dll, "DllGetClassObject");
    real_ddraw.GetDDSurfaceLocal           = GetProcAddress(real_ddraw.dll, "GetDDSurfaceLocal");
    real_ddraw.GetOLEThunkData             = GetProcAddress(real_ddraw.dll, "GetOLEThunkData");
    real_ddraw.GetSurfaceFromDC            = GetProcAddress(real_ddraw.dll, "GetSurfaceFromDC");
    real_ddraw.RegisterSpecialCase         = GetProcAddress(real_ddraw.dll, "RegisterSpecialCase");
    real_ddraw.ReleaseDDThreadLock         = GetProcAddress(real_ddraw.dll, "ReleaseDDThreadLock");
}

#define DEFINE_NAKED_FORWARDER(name) \
    extern "C" __attribute__((naked)) void Fake##name() { \
        asm volatile ("jmp *%0" : : "m"(real_ddraw.name)); \
    }

DEFINE_NAKED_FORWARDER(AcquireDDThreadLock)
DEFINE_NAKED_FORWARDER(CheckFullscreen)
DEFINE_NAKED_FORWARDER(CompleteCreateSysmemSurface)
DEFINE_NAKED_FORWARDER(D3DParseUnknownCommand)
DEFINE_NAKED_FORWARDER(DDGetAttachedSurfaceLcl)
DEFINE_NAKED_FORWARDER(DDInternalLock)
DEFINE_NAKED_FORWARDER(DDInternalUnlock)
DEFINE_NAKED_FORWARDER(DSoundHelp)
DEFINE_NAKED_FORWARDER(DirectDrawCreate)
DEFINE_NAKED_FORWARDER(DirectDrawCreateClipper)
DEFINE_NAKED_FORWARDER(DirectDrawEnumerateA)
DEFINE_NAKED_FORWARDER(DirectDrawEnumerateExA)
DEFINE_NAKED_FORWARDER(DirectDrawEnumerateExW)
DEFINE_NAKED_FORWARDER(DirectDrawEnumerateW)
DEFINE_NAKED_FORWARDER(DllCanUnloadNow)
DEFINE_NAKED_FORWARDER(DllGetClassObject)
DEFINE_NAKED_FORWARDER(GetDDSurfaceLocal)
DEFINE_NAKED_FORWARDER(GetOLEThunkData)
DEFINE_NAKED_FORWARDER(GetSurfaceFromDC)
DEFINE_NAKED_FORWARDER(RegisterSpecialCase)
DEFINE_NAKED_FORWARDER(ReleaseDDThreadLock)

extern "C" __attribute__((stdcall))
HRESULT HookedDirectDrawCreateEx(GUID* lpGuid, void** lplpDD, REFIID iid, IUnknown* pUnkOuter) {
    printf("[GVulkan] HookedDirectDrawCreateEx called\n");

    *lplpDD = new StubDirectDraw7();
    return S_OK;
}

BOOL WINAPI DllMain(HINSTANCE hInst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hInst);

        AllocConsole();
        freopen("CONOUT$", "w", stdout);
        freopen("CONOUT$", "w", stderr);
        printf("[GVulkan] DLL loaded - ddraw.dll proxy active (Vulkan backend)\n");

        LoadRealDDraw();
    } else if (reason == DLL_PROCESS_DETACH) {
        printf("[GVulkan] DLL unloading\n");
        VkRenderer::Shutdown();
        GVulkan_Stop();
        if (real_ddraw.dll) {
            FreeLibrary(real_ddraw.dll);
            real_ddraw.dll = nullptr;
        }
        FreeConsole();
    }
    return TRUE;
}
