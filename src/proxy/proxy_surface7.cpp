#include "proxy_surface7.h"
#include "../debug.h"
#include "../renderer/gl_renderer.h"
#include <cstring>

static bool g_framePresentedByFlip = false;

void GOpenGL_ResetPresentFlag() {
    g_framePresentedByFlip = false;
}

void StubDirectDrawSurface7::EnsureSurfaceBuffer() {
    if (surfaceData.empty() && desc.dwWidth > 0 && desc.dwHeight > 0) {
        if (desc.ddpfPixelFormat.dwFlags & DDPF_FOURCC) {
            DWORD blockSize = 16;
            if (desc.ddpfPixelFormat.dwFourCC == FOURCC_DXT1) blockSize = 8;
            DWORD blocksW = (desc.dwWidth + 3) / 4;
            DWORD blocksH = (desc.dwHeight + 3) / 4;
            desc.lPitch = blocksW * blockSize;
            surfaceData.resize(blocksW * blocksH * blockSize, 0);
        } else {
            DWORD bpp = desc.ddpfPixelFormat.dwRGBBitCount;
            if (bpp == 0) bpp = desc.ddpfPixelFormat.dwZBufferBitDepth;
            if (bpp == 0) bpp = 32;
            DWORD pitch = desc.dwWidth * ((bpp + 7) / 8);
            desc.lPitch = pitch;
            surfaceData.resize(pitch * desc.dwHeight, 0);
        }
        desc.lpSurface = surfaceData.data();
    }
}

StubDirectDrawSurface7::StubDirectDrawSurface7(const char* surfaceTag) : tag(surfaceTag) {
    memset(&desc, 0, sizeof(desc));
    desc.dwSize = sizeof(desc);
}

StubDirectDrawSurface7::~StubDirectDrawSurface7() {
    if (glTextureId) {
        GLRenderer::FreeTexture(glTextureId);
        glTextureId = 0;
    }
}

void StubDirectDrawSurface7::SetDesc(LPDDSURFACEDESC2 d) {
    if (d) {
        desc = *d;
        desc.dwSize = sizeof(DDSURFACEDESC2);
    }
}

void StubDirectDrawSurface7::InitAsRGB(DWORD w, DWORD h, DWORD bpp) {
    desc.dwWidth = w;
    desc.dwHeight = h;
    desc.dwFlags |= DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT | DDSD_PITCH | DDSD_CAPS;
    desc.ddpfPixelFormat.dwSize = sizeof(DDPIXELFORMAT);
    desc.ddpfPixelFormat.dwFlags = DDPF_RGB;
    desc.ddpfPixelFormat.dwRGBBitCount = bpp;
    if (bpp == 32) {
        desc.ddpfPixelFormat.dwRBitMask = 0xFF0000;
        desc.ddpfPixelFormat.dwGBitMask = 0xFF00;
        desc.ddpfPixelFormat.dwBBitMask = 0xFF;
        desc.ddpfPixelFormat.dwRGBAlphaBitMask = 0xFF000000;
        desc.ddpfPixelFormat.dwFlags |= DDPF_ALPHAPIXELS;
    }
    desc.lPitch = w * (bpp / 8);
    EnsureSurfaceBuffer();
}

void StubDirectDrawSurface7::InitAsZBuffer(DWORD w, DWORD h, const DDPIXELFORMAT& fmt) {
    desc.dwWidth = w;
    desc.dwHeight = h;
    desc.dwFlags |= DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT | DDSD_PITCH | DDSD_CAPS;
    desc.ddpfPixelFormat = fmt;
    desc.ddpfPixelFormat.dwSize = sizeof(DDPIXELFORMAT);
    DWORD bpp = fmt.dwZBufferBitDepth > 0 ? fmt.dwZBufferBitDepth : 32;
    desc.lPitch = w * ((bpp + 7) / 8);
    EnsureSurfaceBuffer();
}

void StubDirectDrawSurface7::InitAsFourCC(DWORD w, DWORD h, DWORD fourCC) {
    desc.dwWidth = w;
    desc.dwHeight = h;
    desc.dwFlags |= DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT | DDSD_CAPS;
    desc.ddpfPixelFormat.dwSize = sizeof(DDPIXELFORMAT);
    desc.ddpfPixelFormat.dwFlags = DDPF_FOURCC;
    desc.ddpfPixelFormat.dwFourCC = fourCC;
    EnsureSurfaceBuffer();
}

void StubDirectDrawSurface7::AttachBackBuffer(StubDirectDrawSurface7* bb) {
    attached = bb;
    if (attached) attached->AddRef();
}

void StubDirectDrawSurface7::UploadTextureToGL() {
    if (!textureDirty) return;
    textureDirty = false;

    if (surfaceData.empty() || desc.dwWidth == 0 || desc.dwHeight == 0) return;

    if (glTextureId == 0) {
        glTextureId = GLRenderer::UploadTexture(
            desc.dwWidth, desc.dwHeight,
            surfaceData.data(), desc.lPitch,
            desc.ddpfPixelFormat);
    } else {
        GLRenderer::UpdateTexture(
            glTextureId,
            desc.dwWidth, desc.dwHeight,
            surfaceData.data(), desc.lPitch,
            desc.ddpfPixelFormat);
    }
}

HRESULT STDMETHODCALLTYPE StubDirectDrawSurface7::QueryInterface(REFIID, void** ppv) {
    DbgPrint("  Surf[%s]::QueryInterface", tag);
    *ppv = this; AddRef(); return S_OK;
}

ULONG STDMETHODCALLTYPE StubDirectDrawSurface7::AddRef() { return ++refCount; }

ULONG STDMETHODCALLTYPE StubDirectDrawSurface7::Release() {
    if (--refCount == 0) {
        if (attached) { attached->Release(); attached = nullptr; }
        delete this;
        return 0;
    }
    return refCount;
}

HRESULT STDMETHODCALLTYPE StubDirectDrawSurface7::AddAttachedSurface(LPDIRECTDRAWSURFACE7 s) {
    DbgPrint("  Surf[%s]::AddAttachedSurface ptr=0x%p", tag, s);
    if (!attached) {
        attached = static_cast<StubDirectDrawSurface7*>(s);
        if (attached) attached->AddRef();
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE StubDirectDrawSurface7::AddOverlayDirtyRect(LPRECT) { return S_OK; }

HRESULT STDMETHODCALLTYPE StubDirectDrawSurface7::Blt(LPRECT, LPDIRECTDRAWSURFACE7, LPRECT, DWORD, LPDDBLTFX) {
    if (isPrimary && !g_framePresentedByFlip) {
        GOpenGL_OnPresent();
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE StubDirectDrawSurface7::BltBatch(LPDDBLTBATCH, DWORD, DWORD) { return S_OK; }
HRESULT STDMETHODCALLTYPE StubDirectDrawSurface7::BltFast(DWORD, DWORD, LPDIRECTDRAWSURFACE7, LPRECT, DWORD) { return S_OK; }

HRESULT STDMETHODCALLTYPE StubDirectDrawSurface7::DeleteAttachedSurface(DWORD, LPDIRECTDRAWSURFACE7) {
    DbgPrint("  Surf[%s]::DeleteAttachedSurface", tag);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE StubDirectDrawSurface7::EnumAttachedSurfaces(LPVOID ctx, LPDDENUMSURFACESCALLBACK7 cb) {
    DbgPrint("  Surf[%s]::EnumAttachedSurfaces", tag);
    if (attached && cb) {
        DDSURFACEDESC2 d = {};
        d.dwSize = sizeof(d);
        attached->GetSurfaceDesc(&d);
        cb(attached, &d, ctx);
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE StubDirectDrawSurface7::EnumOverlayZOrders(DWORD, LPVOID, LPDDENUMSURFACESCALLBACK7) { return S_OK; }

HRESULT STDMETHODCALLTYPE StubDirectDrawSurface7::Flip(LPDIRECTDRAWSURFACE7, DWORD) {
    GOpenGL_OnPresent();
    g_framePresentedByFlip = true;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE StubDirectDrawSurface7::GetAttachedSurface(LPDDSCAPS2 caps, LPDIRECTDRAWSURFACE7* s) {
    DbgPrint("  Surf[%s]::GetAttachedSurface caps=0x%X", tag, caps ? caps->dwCaps : 0);
    if (attached) {
        attached->AddRef();
        *s = attached;
        return S_OK;
    }
    return DDERR_NOTFOUND;
}

HRESULT STDMETHODCALLTYPE StubDirectDrawSurface7::GetBltStatus(DWORD) { return S_OK; }

HRESULT STDMETHODCALLTYPE StubDirectDrawSurface7::GetCaps(LPDDSCAPS2 caps) {
    DbgPrint("  Surf[%s]::GetCaps", tag);
    if (caps) *caps = desc.ddsCaps;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE StubDirectDrawSurface7::GetClipper(LPDIRECTDRAWCLIPPER*) { return DDERR_NOCLIPPERATTACHED; }
HRESULT STDMETHODCALLTYPE StubDirectDrawSurface7::GetColorKey(DWORD, LPDDCOLORKEY) { return DDERR_NOCOLORKEY; }
HRESULT STDMETHODCALLTYPE StubDirectDrawSurface7::GetDC(HDC* h) { if (h) *h = nullptr; return S_OK; }
HRESULT STDMETHODCALLTYPE StubDirectDrawSurface7::GetFlipStatus(DWORD) { return S_OK; }
HRESULT STDMETHODCALLTYPE StubDirectDrawSurface7::GetOverlayPosition(LPLONG, LPLONG) { return S_OK; }
HRESULT STDMETHODCALLTYPE StubDirectDrawSurface7::GetPalette(LPDIRECTDRAWPALETTE*) { return DDERR_NOPALETTEATTACHED; }

HRESULT STDMETHODCALLTYPE StubDirectDrawSurface7::GetPixelFormat(LPDDPIXELFORMAT pf) {
    DbgPrint("  Surf[%s]::GetPixelFormat", tag);
    if (pf) *pf = desc.ddpfPixelFormat;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE StubDirectDrawSurface7::GetSurfaceDesc(LPDDSURFACEDESC2 d) {
    DbgPrint("  Surf[%s]::GetSurfaceDesc", tag);
    if (d) { *d = desc; d->dwSize = sizeof(DDSURFACEDESC2); }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE StubDirectDrawSurface7::Initialize(LPDIRECTDRAW, LPDDSURFACEDESC2) {
    DbgPrint("  Surf[%s]::Initialize", tag);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE StubDirectDrawSurface7::IsLost() { return S_OK; }

HRESULT STDMETHODCALLTYPE StubDirectDrawSurface7::Lock(LPRECT, LPDDSURFACEDESC2 d, DWORD, HANDLE) {
    EnsureSurfaceBuffer();
    if (d) {
        *d = desc;
        d->dwSize = sizeof(DDSURFACEDESC2);
        if (!surfaceData.empty()) {
            d->lpSurface = surfaceData.data();
            d->lPitch = desc.lPitch;
        } else {
            static unsigned char dummyBuf[16] = {};
            d->lpSurface = dummyBuf;
            d->lPitch = 4;
        }
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE StubDirectDrawSurface7::ReleaseDC(HDC) { return S_OK; }
HRESULT STDMETHODCALLTYPE StubDirectDrawSurface7::Restore() { return S_OK; }

HRESULT STDMETHODCALLTYPE StubDirectDrawSurface7::SetClipper(LPDIRECTDRAWCLIPPER clipper) {
    if (clipper) {
        HWND hwnd = nullptr;
        clipper->GetHWnd(&hwnd);
        if (hwnd) {
            DbgPrint("  Surf[%s]::SetClipper -> got HWND 0x%p", tag, hwnd);
            GOpenGL_OnSetWindow(hwnd);
        }
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE StubDirectDrawSurface7::SetColorKey(DWORD, LPDDCOLORKEY) { return S_OK; }
HRESULT STDMETHODCALLTYPE StubDirectDrawSurface7::SetOverlayPosition(LONG, LONG) { return S_OK; }
HRESULT STDMETHODCALLTYPE StubDirectDrawSurface7::SetPalette(LPDIRECTDRAWPALETTE) { return S_OK; }

HRESULT STDMETHODCALLTYPE StubDirectDrawSurface7::Unlock(LPRECT) {
    textureDirty = true;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE StubDirectDrawSurface7::UpdateOverlay(LPRECT, LPDIRECTDRAWSURFACE7, LPRECT, DWORD, LPDDOVERLAYFX) { return S_OK; }
HRESULT STDMETHODCALLTYPE StubDirectDrawSurface7::UpdateOverlayDisplay(DWORD) { return S_OK; }
HRESULT STDMETHODCALLTYPE StubDirectDrawSurface7::UpdateOverlayZOrder(DWORD, LPDIRECTDRAWSURFACE7) { return S_OK; }
HRESULT STDMETHODCALLTYPE StubDirectDrawSurface7::GetDDInterface(LPVOID*) { return S_OK; }
HRESULT STDMETHODCALLTYPE StubDirectDrawSurface7::PageLock(DWORD) { return S_OK; }
HRESULT STDMETHODCALLTYPE StubDirectDrawSurface7::PageUnlock(DWORD) { return S_OK; }

HRESULT STDMETHODCALLTYPE StubDirectDrawSurface7::SetSurfaceDesc(LPDDSURFACEDESC2 d, DWORD) {
    DbgPrint("  Surf[%s]::SetSurfaceDesc", tag);
    if (d) {
        desc = *d;
        desc.dwSize = sizeof(DDSURFACEDESC2);
        surfaceData.clear();
        EnsureSurfaceBuffer();
        textureDirty = true;
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE StubDirectDrawSurface7::SetPrivateData(REFGUID, LPVOID, DWORD, DWORD) { return S_OK; }
HRESULT STDMETHODCALLTYPE StubDirectDrawSurface7::GetPrivateData(REFGUID, LPVOID, LPDWORD) { return S_OK; }
HRESULT STDMETHODCALLTYPE StubDirectDrawSurface7::FreePrivateData(REFGUID) { return S_OK; }
HRESULT STDMETHODCALLTYPE StubDirectDrawSurface7::GetUniquenessValue(LPDWORD v) { if (v) *v = 0; return S_OK; }
HRESULT STDMETHODCALLTYPE StubDirectDrawSurface7::ChangeUniquenessValue() { return S_OK; }
HRESULT STDMETHODCALLTYPE StubDirectDrawSurface7::SetPriority(DWORD) { return S_OK; }
HRESULT STDMETHODCALLTYPE StubDirectDrawSurface7::GetPriority(LPDWORD p) { if (p) *p = 0; return S_OK; }
HRESULT STDMETHODCALLTYPE StubDirectDrawSurface7::SetLOD(DWORD) { return S_OK; }
HRESULT STDMETHODCALLTYPE StubDirectDrawSurface7::GetLOD(LPDWORD l) { if (l) *l = 0; return S_OK; }
