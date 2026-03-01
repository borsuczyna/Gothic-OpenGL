#include "proxy_ddraw7.h"
#include "proxy_d3d7.h"
#include "proxy_surface7.h"
#include "proxy_clipper.h"
#include "../debug.h"
#include "../renderer/gl_window.h"
#include <cstring>

StubDirectDraw7::StubDirectDraw7() {
    DbgPrint("StubDirectDraw7 created");
    memset(&displayMode, 0, sizeof(displayMode));
    displayMode.dwSize = sizeof(displayMode);
    displayMode.dwWidth = 800;
    displayMode.dwHeight = 600;
    displayMode.dwRefreshRate = 60;
    displayMode.dwFlags = DDSD_HEIGHT | DDSD_WIDTH | DDSD_PIXELFORMAT;
    displayMode.ddpfPixelFormat.dwSize = sizeof(DDPIXELFORMAT);
    displayMode.ddpfPixelFormat.dwFlags = DDPF_RGB;
    displayMode.ddpfPixelFormat.dwRGBBitCount = 32;
    displayMode.ddpfPixelFormat.dwRBitMask = 0xFF0000;
    displayMode.ddpfPixelFormat.dwGBitMask = 0xFF00;
    displayMode.ddpfPixelFormat.dwBBitMask = 0xFF;
}

HRESULT STDMETHODCALLTYPE StubDirectDraw7::QueryInterface(REFIID riid, void** ppv) {
    DbgPrint("DirectDraw7::QueryInterface");
    if (riid == IID_IDirect3D7) {
        *ppv = new StubDirect3D7();
        return S_OK;
    }
    *ppv = this;
    AddRef();
    return S_OK;
}

ULONG STDMETHODCALLTYPE StubDirectDraw7::AddRef() { return ++refCount; }
ULONG STDMETHODCALLTYPE StubDirectDraw7::Release() { if (--refCount == 0) { delete this; return 0; } return refCount; }

HRESULT STDMETHODCALLTYPE StubDirectDraw7::Compact() { return S_OK; }

HRESULT STDMETHODCALLTYPE StubDirectDraw7::CreateClipper(DWORD, LPDIRECTDRAWCLIPPER* c, IUnknown*) {
    DbgPrint("DirectDraw7::CreateClipper");
    *c = new StubDirectDrawClipper();
    return S_OK;
}

HRESULT STDMETHODCALLTYPE StubDirectDraw7::CreatePalette(DWORD, LPPALETTEENTRY, LPDIRECTDRAWPALETTE*, IUnknown*) { return S_OK; }

HRESULT STDMETHODCALLTYPE StubDirectDraw7::CreateSurface(LPDDSURFACEDESC2 d, LPDIRECTDRAWSURFACE7* s, IUnknown*) {
    DWORD caps = d ? d->ddsCaps.dwCaps : 0;
    DbgPrint("DirectDraw7::CreateSurface caps=0x%X size=%ux%u", caps, d ? d->dwWidth : 0, d ? d->dwHeight : 0);
    if (d) {
        DbgPrint("  desc flags=0x%X caps2=0x%X pfFlags=0x%X pfBitCount=%u pfZDepth=%u",
            d->dwFlags, d->ddsCaps.dwCaps2,
            d->ddpfPixelFormat.dwFlags,
            d->ddpfPixelFormat.dwRGBBitCount,
            d->ddpfPixelFormat.dwZBufferBitDepth);
    }

    DWORD w = (d && d->dwWidth > 0)  ? d->dwWidth  : displayMode.dwWidth;
    DWORD h = (d && d->dwHeight > 0) ? d->dwHeight : displayMode.dwHeight;

    if (caps & DDSCAPS_PRIMARYSURFACE) {
        auto* primary = new StubDirectDrawSurface7("primary");
        if (d) primary->SetDesc(d);
        primary->InitAsRGB(w, h, 32);

        auto* backBuf = new StubDirectDrawSurface7("backbuf");
        backBuf->InitAsRGB(w, h, 32);
        primary->AttachBackBuffer(backBuf);

        *s = primary;
        void** vtP = *(void***)primary;
        DbgPrint("  -> Primary@0x%p vtable@0x%p BackBuffer@0x%p %ux%u@32bpp", primary, vtP, backBuf, w, h);
    } else if (caps & DDSCAPS_ZBUFFER) {
        DbgPrint("  -> Creating zbuffer...");
        auto* zbuf = new StubDirectDrawSurface7("zbuffer");
        DbgPrint("  -> zbuf allocated at 0x%p", zbuf);
        if (d) {
            zbuf->SetDesc(d);
            DbgPrint("  -> SetDesc done");
            zbuf->InitAsZBuffer(w, h, d->ddpfPixelFormat);
            DbgPrint("  -> InitAsZBuffer done");
        }
        *s = zbuf;
        void** vtable = *(void***)zbuf;
        DbgPrint("  -> ZBuffer@0x%p vtable@0x%p [0]=0x%p [1]=0x%p [2]=0x%p [3]=0x%p",
            zbuf, vtable, vtable[0], vtable[1], vtable[2], vtable[3]);
        DbgPrint("  -> ZBuffer %ux%u depth=%u DONE", w, h, d ? d->ddpfPixelFormat.dwZBufferBitDepth : 0);
    } else {
        DWORD tw = (d && d->dwWidth > 0)  ? d->dwWidth  : 256;
        DWORD th = (d && d->dwHeight > 0) ? d->dwHeight : 256;
        DWORD tbpp = (d && d->ddpfPixelFormat.dwRGBBitCount > 0) ? d->ddpfPixelFormat.dwRGBBitCount : 32;
        bool isFourCC = d && (d->ddpfPixelFormat.dwFlags & DDPF_FOURCC);

        auto* surf = new StubDirectDrawSurface7("texture");
        if (d) surf->SetDesc(d);
        if (!isFourCC) {
            surf->InitAsRGB(tw, th, tbpp);
        } else {
            surf->InitAsRGB(tw, th, 32);
        }

        if (caps & DDSCAPS_MIPMAP) {
            StubDirectDrawSurface7* prev = surf;
            DWORD mw = tw / 2, mh = th / 2;
            while (mw >= 1 && mh >= 1) {
                auto* mip = new StubDirectDrawSurface7("mip");
                if (d) mip->SetDesc(d);
                if (!isFourCC) {
                    mip->InitAsRGB(mw, mh, tbpp);
                } else {
                    mip->InitAsRGB(mw > 4 ? mw : 4, mh > 4 ? mh : 4, 32);
                }
                prev->AttachBackBuffer(mip);
                prev = mip;
                if (mw == 1 && mh == 1) break;
                mw = mw > 1 ? mw / 2 : 1;
                mh = mh > 1 ? mh / 2 : 1;
            }
            DbgPrint("  -> Texture+MipChain %ux%u", tw, th);
        }

        *s = surf;
    }
    DbgPrint("DirectDraw7::CreateSurface RETURNING S_OK");
    return S_OK;
}

HRESULT STDMETHODCALLTYPE StubDirectDraw7::DuplicateSurface(LPDIRECTDRAWSURFACE7, LPDIRECTDRAWSURFACE7* s) {
    if (s) *s = new StubDirectDrawSurface7("dup");
    return S_OK;
}

HRESULT STDMETHODCALLTYPE StubDirectDraw7::EnumDisplayModes(DWORD flags, LPDDSURFACEDESC2, LPVOID ctx, LPDDENUMMODESCALLBACK2 cb) {
    DbgPrint("DirectDraw7::EnumDisplayModes");
    DDSURFACEDESC2 mode = {};
    mode.dwSize = sizeof(DDSURFACEDESC2);
    mode.dwFlags = DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT;
    mode.ddpfPixelFormat.dwSize = sizeof(DDPIXELFORMAT);
    mode.ddpfPixelFormat.dwRGBBitCount = 32;

    int resolutions[][2] = {{640, 480}, {800, 600}, {1024, 768}, {1280, 720}, {1920, 1080}};
    for (auto& r : resolutions) {
        mode.dwWidth = r[0];
        mode.dwHeight = r[1];
        if (flags & DDEDM_REFRESHRATES) mode.dwRefreshRate = 60;
        cb(&mode, ctx);
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE StubDirectDraw7::EnumSurfaces(DWORD, LPDDSURFACEDESC2, LPVOID, LPDDENUMSURFACESCALLBACK7) { return S_OK; }
HRESULT STDMETHODCALLTYPE StubDirectDraw7::FlipToGDISurface() { return S_OK; }

HRESULT STDMETHODCALLTYPE StubDirectDraw7::GetCaps(LPDDCAPS drv, LPDDCAPS hel) {
    if (drv) { memset(drv, 0, sizeof(DDCAPS)); drv->dwSize = sizeof(DDCAPS); }
    if (hel) { memset(hel, 0, sizeof(DDCAPS)); hel->dwSize = sizeof(DDCAPS); }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE StubDirectDraw7::GetDisplayMode(LPDDSURFACEDESC2 d) { if (d) *d = displayMode; return S_OK; }
HRESULT STDMETHODCALLTYPE StubDirectDraw7::GetFourCCCodes(LPDWORD n, LPDWORD) { if (n) *n = 0; return S_OK; }
HRESULT STDMETHODCALLTYPE StubDirectDraw7::GetGDISurface(LPDIRECTDRAWSURFACE7* pp) { if (pp) *pp = nullptr; return S_OK; }
HRESULT STDMETHODCALLTYPE StubDirectDraw7::GetMonitorFrequency(LPDWORD f) { if (f) *f = 60; return S_OK; }
HRESULT STDMETHODCALLTYPE StubDirectDraw7::GetScanLine(LPDWORD l) { if (l) *l = 0; return S_OK; }
HRESULT STDMETHODCALLTYPE StubDirectDraw7::GetVerticalBlankStatus(LPBOOL b) { if (b) *b = FALSE; return S_OK; }
HRESULT STDMETHODCALLTYPE StubDirectDraw7::Initialize(GUID*) { return S_OK; }
HRESULT STDMETHODCALLTYPE StubDirectDraw7::RestoreDisplayMode() { return S_OK; }

HRESULT STDMETHODCALLTYPE StubDirectDraw7::SetCooperativeLevel(HWND hwnd, DWORD) {
    DbgPrint("DirectDraw7::SetCooperativeLevel hwnd=0x%p", hwnd);
    if (hwnd) {
        GOpenGL_OnSetWindow(hwnd);
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE StubDirectDraw7::SetDisplayMode(DWORD w, DWORD h, DWORD bpp, DWORD refresh, DWORD) {
    DbgPrint("DirectDraw7::SetDisplayMode %ux%u@%ubpp", w, h, bpp);
    displayMode.dwWidth = w;
    displayMode.dwHeight = h;
    displayMode.dwRefreshRate = refresh;
    displayMode.ddpfPixelFormat.dwRGBBitCount = bpp;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE StubDirectDraw7::WaitForVerticalBlank(DWORD, HANDLE) { return S_OK; }

HRESULT STDMETHODCALLTYPE StubDirectDraw7::GetAvailableVidMem(LPDDSCAPS2, LPDWORD total, LPDWORD free) {
    if (total) *total = 512 * 1024 * 1024;
    if (free)  *free  = 256 * 1024 * 1024;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE StubDirectDraw7::GetSurfaceFromDC(HDC, LPDIRECTDRAWSURFACE7* pp) { if (pp) *pp = nullptr; return S_OK; }
HRESULT STDMETHODCALLTYPE StubDirectDraw7::RestoreAllSurfaces() { return S_OK; }
HRESULT STDMETHODCALLTYPE StubDirectDraw7::TestCooperativeLevel() { return S_OK; }

HRESULT STDMETHODCALLTYPE StubDirectDraw7::GetDeviceIdentifier(LPDDDEVICEIDENTIFIER2 id, DWORD) {
    if (id) {
        memset(id, 0, sizeof(DDDEVICEIDENTIFIER2));
        strcpy(id->szDescription, "GOpenGL Renderer");
        strcpy(id->szDriver, "GOpenGL");
        id->guidDeviceIdentifier = {0xF5049E78, 0x4861, 0x11D2, {0xA4, 0x07, 0x00, 0xA0, 0xC9, 0x06, 0x29, 0xA8}};
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE StubDirectDraw7::StartModeTest(LPSIZE, DWORD, DWORD) { return S_OK; }
HRESULT STDMETHODCALLTYPE StubDirectDraw7::EvaluateMode(DWORD, DWORD*) { return S_OK; }
