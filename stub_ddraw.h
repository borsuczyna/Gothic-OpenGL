#pragma once
#include <ddraw.h>
#include <d3d.h>
#include <cstdio>
#include <cstring>
#include <vector>

#ifndef FOURCC_DXT1
#define FOURCC_DXT1 MAKEFOURCC('D','X','T','1')
#define FOURCC_DXT2 MAKEFOURCC('D','X','T','2')
#define FOURCC_DXT3 MAKEFOURCC('D','X','T','3')
#define FOURCC_DXT4 MAKEFOURCC('D','X','T','4')
#define FOURCC_DXT5 MAKEFOURCC('D','X','T','5')
#endif

static void DbgPrint(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    printf("[GVLK] ");
    vprintf(fmt, args);
    printf("\n");
    fflush(stdout);
    va_end(args);
}

class StubDirectDraw7;
class StubDirect3D7;
class StubDirect3DDevice7;
class StubDirectDrawSurface7;
class StubDirectDrawClipper;
class StubDirect3DVertexBuffer7;

// ============================================================
// StubDirectDrawClipper
// ============================================================
class StubDirectDrawClipper : public IDirectDrawClipper {
    int refCount = 1;
    HWND clipperHwnd = nullptr;
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void** ppv) override { *ppv = this; AddRef(); return S_OK; }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refCount; }
    ULONG STDMETHODCALLTYPE Release() override { if (--refCount == 0) { delete this; return 0; } return refCount; }
    HRESULT STDMETHODCALLTYPE GetClipList(LPRECT, LPRGNDATA, LPDWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetHWnd(HWND* h) override { if (h) *h = clipperHwnd; return S_OK; }
    HRESULT STDMETHODCALLTYPE Initialize(LPDIRECTDRAW, DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE IsClipListChanged(BOOL* b) override { if (b) *b = FALSE; return S_OK; }
    HRESULT STDMETHODCALLTYPE SetClipList(LPRGNDATA, DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE SetHWnd(DWORD, HWND h) override { clipperHwnd = h; return S_OK; }
};

// ============================================================
// StubDirectDrawSurface7
// ============================================================
class StubDirectDrawSurface7 : public IDirectDrawSurface7 {
    int refCount = 1;
    DDSURFACEDESC2 desc;
    StubDirectDrawSurface7* attached = nullptr;
    std::vector<unsigned char> surfaceData;
    const char* tag = "unknown";

    void EnsureSurfaceBuffer() {
        if (surfaceData.empty() && desc.dwWidth > 0 && desc.dwHeight > 0) {
            DWORD bpp = desc.ddpfPixelFormat.dwRGBBitCount;
            if (bpp == 0) bpp = desc.ddpfPixelFormat.dwZBufferBitDepth;
            if (bpp == 0) bpp = 32;
            DWORD pitch = desc.dwWidth * ((bpp + 7) / 8);
            desc.lPitch = pitch;
            surfaceData.resize(pitch * desc.dwHeight, 0);
            desc.lpSurface = surfaceData.data();
        }
    }

public:
    StubDirectDrawSurface7(const char* surfaceTag = "surface") : tag(surfaceTag) {
        memset(&desc, 0, sizeof(desc));
        desc.dwSize = sizeof(desc);
    }

    void SetDesc(LPDDSURFACEDESC2 d) {
        if (d) {
            desc = *d;
            desc.dwSize = sizeof(DDSURFACEDESC2);
        }
    }

    void InitAsRGB(DWORD w, DWORD h, DWORD bpp) {
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

    void InitAsZBuffer(DWORD w, DWORD h, const DDPIXELFORMAT& fmt) {
        desc.dwWidth = w;
        desc.dwHeight = h;
        desc.dwFlags |= DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT | DDSD_PITCH | DDSD_CAPS;
        desc.ddpfPixelFormat = fmt;
        desc.ddpfPixelFormat.dwSize = sizeof(DDPIXELFORMAT);
        DWORD bpp = fmt.dwZBufferBitDepth > 0 ? fmt.dwZBufferBitDepth : 32;
        desc.lPitch = w * ((bpp + 7) / 8);
        EnsureSurfaceBuffer();
    }

    void AttachBackBuffer(StubDirectDrawSurface7* bb) {
        attached = bb;
        if (attached) attached->AddRef();
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void** ppv) override {
        DbgPrint("  Surf[%s]::QueryInterface", tag);
        *ppv = this; AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refCount; }
    ULONG STDMETHODCALLTYPE Release() override {
        if (--refCount == 0) {
            if (attached) { attached->Release(); attached = nullptr; }
            delete this;
            return 0;
        }
        return refCount;
    }

    HRESULT STDMETHODCALLTYPE AddAttachedSurface(LPDIRECTDRAWSURFACE7 s) override {
        DbgPrint("  Surf[%s]::AddAttachedSurface ptr=0x%p", tag, s);
        if (!attached) {
            attached = static_cast<StubDirectDrawSurface7*>(s);
            if (attached) attached->AddRef();
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE AddOverlayDirtyRect(LPRECT) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE Blt(LPRECT, LPDIRECTDRAWSURFACE7, LPRECT, DWORD, LPDDBLTFX) override {
        GVLK_OnPresent();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE BltBatch(LPDDBLTBATCH, DWORD, DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE BltFast(DWORD, DWORD, LPDIRECTDRAWSURFACE7, LPRECT, DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE DeleteAttachedSurface(DWORD, LPDIRECTDRAWSURFACE7) override { DbgPrint("  Surf[%s]::DeleteAttachedSurface", tag); return S_OK; }
    HRESULT STDMETHODCALLTYPE EnumAttachedSurfaces(LPVOID ctx, LPDDENUMSURFACESCALLBACK7 cb) override {
        DbgPrint("  Surf[%s]::EnumAttachedSurfaces", tag);
        if (attached && cb) {
            DDSURFACEDESC2 d = {};
            d.dwSize = sizeof(d);
            attached->GetSurfaceDesc(&d);
            cb(attached, &d, ctx);
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE EnumOverlayZOrders(DWORD, LPVOID, LPDDENUMSURFACESCALLBACK7) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE Flip(LPDIRECTDRAWSURFACE7, DWORD) override {
        GVLK_OnPresent();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetAttachedSurface(LPDDSCAPS2 caps, LPDIRECTDRAWSURFACE7* s) override {
        DbgPrint("  Surf[%s]::GetAttachedSurface caps=0x%X", tag, caps ? caps->dwCaps : 0);
        if (attached) {
            attached->AddRef();
            *s = attached;
            return S_OK;
        }
        return DDERR_NOTFOUND;
    }
    HRESULT STDMETHODCALLTYPE GetBltStatus(DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetCaps(LPDDSCAPS2 caps) override {
        DbgPrint("  Surf[%s]::GetCaps", tag);
        if (caps) *caps = desc.ddsCaps;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetClipper(LPDIRECTDRAWCLIPPER*) override { return DDERR_NOCLIPPERATTACHED; }
    HRESULT STDMETHODCALLTYPE GetColorKey(DWORD, LPDDCOLORKEY) override { return DDERR_NOCOLORKEY; }
    HRESULT STDMETHODCALLTYPE GetDC(HDC* h) override { if (h) *h = nullptr; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetFlipStatus(DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetOverlayPosition(LPLONG, LPLONG) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetPalette(LPDIRECTDRAWPALETTE*) override { return DDERR_NOPALETTEATTACHED; }
    HRESULT STDMETHODCALLTYPE GetPixelFormat(LPDDPIXELFORMAT pf) override {
        DbgPrint("  Surf[%s]::GetPixelFormat", tag);
        if (pf) *pf = desc.ddpfPixelFormat;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetSurfaceDesc(LPDDSURFACEDESC2 d) override {
        DbgPrint("  Surf[%s]::GetSurfaceDesc", tag);
        if (d) { *d = desc; d->dwSize = sizeof(DDSURFACEDESC2); }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Initialize(LPDIRECTDRAW, LPDDSURFACEDESC2) override { DbgPrint("  Surf[%s]::Initialize", tag); return S_OK; }
    HRESULT STDMETHODCALLTYPE IsLost() override { return S_OK; }

    HRESULT STDMETHODCALLTYPE Lock(LPRECT, LPDDSURFACEDESC2 d, DWORD, HANDLE) override {
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

    HRESULT STDMETHODCALLTYPE ReleaseDC(HDC) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE Restore() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE SetClipper(LPDIRECTDRAWCLIPPER clipper) override {
        if (clipper) {
            HWND hwnd = nullptr;
            clipper->GetHWnd(&hwnd);
            if (hwnd) {
                DbgPrint("  Surf[%s]::SetClipper -> got HWND 0x%p", tag, hwnd);
                GVLK_OnSetWindow(hwnd);
            }
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetColorKey(DWORD, LPDDCOLORKEY) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE SetOverlayPosition(LONG, LONG) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE SetPalette(LPDIRECTDRAWPALETTE) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE Unlock(LPRECT) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE UpdateOverlay(LPRECT, LPDIRECTDRAWSURFACE7, LPRECT, DWORD, LPDDOVERLAYFX) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE UpdateOverlayDisplay(DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE UpdateOverlayZOrder(DWORD, LPDIRECTDRAWSURFACE7) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetDDInterface(LPVOID*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE PageLock(DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE PageUnlock(DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE SetSurfaceDesc(LPDDSURFACEDESC2 d, DWORD) override {
        DbgPrint("  Surf[%s]::SetSurfaceDesc", tag);
        if (d) { desc = *d; desc.dwSize = sizeof(DDSURFACEDESC2); surfaceData.clear(); EnsureSurfaceBuffer(); }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID, LPVOID, DWORD, DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID, LPVOID, LPDWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE FreePrivateData(REFGUID) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetUniquenessValue(LPDWORD v) override { if (v) *v = 0; return S_OK; }
    HRESULT STDMETHODCALLTYPE ChangeUniquenessValue() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE SetPriority(DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetPriority(LPDWORD p) override { if (p) *p = 0; return S_OK; }
    HRESULT STDMETHODCALLTYPE SetLOD(DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetLOD(LPDWORD l) override { if (l) *l = 0; return S_OK; }
};

// ============================================================
// StubDirect3DVertexBuffer7
// ============================================================
class StubDirect3DVertexBuffer7 : public IDirect3DVertexBuffer7 {
    int refCount = 1;
    D3DVERTEXBUFFERDESC vbDesc = {};
    std::vector<unsigned char> data;
public:
    StubDirect3DVertexBuffer7(const D3DVERTEXBUFFERDESC& d) : vbDesc(d) {
        data.resize(d.dwNumVertices * 64, 0);
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void** ppv) override { *ppv = this; AddRef(); return S_OK; }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refCount; }
    ULONG STDMETHODCALLTYPE Release() override { if (--refCount == 0) { delete this; return 0; } return refCount; }
    HRESULT STDMETHODCALLTYPE Lock(DWORD, LPVOID* p, LPDWORD sz) override {
        if (p) *p = data.data();
        if (sz) *sz = static_cast<DWORD>(data.size());
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Unlock() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE ProcessVertices(DWORD, DWORD, DWORD, LPDIRECT3DVERTEXBUFFER7, DWORD, LPDIRECT3DDEVICE7, DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetVertexBufferDesc(LPD3DVERTEXBUFFERDESC d) override { if (d) *d = vbDesc; return S_OK; }
    HRESULT STDMETHODCALLTYPE Optimize(LPDIRECT3DDEVICE7, DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE ProcessVerticesStrided(DWORD, DWORD, DWORD, LPD3DDRAWPRIMITIVESTRIDEDDATA, DWORD, LPDIRECT3DDEVICE7, DWORD) override { return S_OK; }
};

// ============================================================
// StubDirect3DDevice7
// ============================================================
class StubDirect3DDevice7 : public IDirect3DDevice7 {
    int refCount = 1;
    D3DDEVICEDESC7 fakeDesc = {};
public:
    StubDirect3DDevice7() {
        memset(&fakeDesc, 0, sizeof(fakeDesc));
        fakeDesc.dwDevCaps = D3DDEVCAPS_FLOATTLVERTEX | D3DDEVCAPS_EXECUTESYSTEMMEMORY |
            D3DDEVCAPS_TLVERTEXSYSTEMMEMORY | D3DDEVCAPS_TEXTUREVIDEOMEMORY |
            D3DDEVCAPS_DRAWPRIMTLVERTEX | D3DDEVCAPS_CANRENDERAFTERFLIP |
            D3DDEVCAPS_DRAWPRIMITIVES2 | D3DDEVCAPS_DRAWPRIMITIVES2EX |
            D3DDEVCAPS_HWTRANSFORMANDLIGHT | D3DDEVCAPS_HWRASTERIZATION;
        fakeDesc.dpcTriCaps.dwSize = sizeof(D3DPRIMCAPS);
        fakeDesc.dpcTriCaps.dwMiscCaps = D3DPMISCCAPS_MASKZ | D3DPMISCCAPS_CULLNONE | D3DPMISCCAPS_CULLCW | D3DPMISCCAPS_CULLCCW;
        fakeDesc.dpcTriCaps.dwRasterCaps = D3DPRASTERCAPS_DITHER | D3DPRASTERCAPS_ZTEST | D3DPRASTERCAPS_FOGVERTEX | D3DPRASTERCAPS_FOGTABLE | D3DPRASTERCAPS_ANISOTROPY;
        fakeDesc.dpcTriCaps.dwZCmpCaps = 0xFF;
        fakeDesc.dpcTriCaps.dwSrcBlendCaps = 0x1FFF;
        fakeDesc.dpcTriCaps.dwDestBlendCaps = 0x1FFF;
        fakeDesc.dpcTriCaps.dwAlphaCmpCaps = 0xFF;
        fakeDesc.dpcTriCaps.dwShadeCaps = 0xFF;
        fakeDesc.dpcTriCaps.dwTextureCaps = D3DPTEXTURECAPS_PERSPECTIVE | D3DPTEXTURECAPS_ALPHA | D3DPTEXTURECAPS_TRANSPARENCY;
        fakeDesc.dpcTriCaps.dwTextureFilterCaps = 0x3FFFF;
        fakeDesc.dpcTriCaps.dwTextureBlendCaps = 0xFF;
        fakeDesc.dpcTriCaps.dwTextureAddressCaps = 0x1F;
        fakeDesc.dpcLineCaps = fakeDesc.dpcTriCaps;
        fakeDesc.dwDeviceRenderBitDepth = 1280;
        fakeDesc.dwDeviceZBufferBitDepth = 1536;
        fakeDesc.dwMinTextureWidth = 1;
        fakeDesc.dwMinTextureHeight = 1;
        fakeDesc.dwMaxTextureWidth = 16384;
        fakeDesc.dwMaxTextureHeight = 16384;
        fakeDesc.dwMaxTextureRepeat = 32768;
        fakeDesc.dwMaxTextureAspectRatio = 32768;
        fakeDesc.dwMaxAnisotropy = 16;
        fakeDesc.wMaxTextureBlendStages = 4;
        fakeDesc.wMaxSimultaneousTextures = 4;
        fakeDesc.dwMaxActiveLights = 8;
        fakeDesc.dvMaxVertexW = 1e10f;
        fakeDesc.deviceGUID = {0xF5049E78, 0x4861, 0x11D2, {0xA4, 0x07, 0x00, 0xA0, 0xC9, 0x06, 0x29, 0xA8}};
        fakeDesc.wMaxUserClipPlanes = 6;
        fakeDesc.wMaxVertexBlendMatrices = 4;
        fakeDesc.dwVertexProcessingCaps = D3DVTXPCAPS_TEXGEN | D3DVTXPCAPS_MATERIALSOURCE7 | D3DVTXPCAPS_DIRECTIONALLIGHTS | D3DVTXPCAPS_POSITIONALLIGHTS | D3DVTXPCAPS_LOCALVIEWER;
        fakeDesc.dwStencilCaps = 0xFF;
        fakeDesc.dwFVFCaps = D3DFVFCAPS_DONOTSTRIPELEMENTS | 8;
        fakeDesc.dwTextureOpCaps = 0x3FFFFFF;
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void** ppv) override { DbgPrint("Device7::QI"); *ppv = this; AddRef(); return S_OK; }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refCount; }
    ULONG STDMETHODCALLTYPE Release() override { if (--refCount == 0) { delete this; return 0; } return refCount; }
    HRESULT STDMETHODCALLTYPE GetCaps(LPD3DDEVICEDESC7 d) override { DbgPrint("Device7::GetCaps"); if (d) *d = fakeDesc; return S_OK; }
    HRESULT STDMETHODCALLTYPE EnumTextureFormats(LPD3DENUMPIXELFORMATSCALLBACK cb, LPVOID ctx) override {
        DbgPrint("Device7::EnumTextureFormats");
        DDPIXELFORMAT fmts[] = {
            {sizeof(DDPIXELFORMAT), DDPF_RGB, 0, 16, 0xF800, 0x7E0, 0x1F, 0x00},
            {sizeof(DDPIXELFORMAT), DDPF_RGB | DDPF_ALPHAPIXELS, 0, 16, 0x7C00, 0x3E0, 0x1F, 0x8000},
            {sizeof(DDPIXELFORMAT), DDPF_RGB | DDPF_ALPHAPIXELS, 0, 16, 0xF00, 0xF0, 0x0F, 0xF000},
            {sizeof(DDPIXELFORMAT), DDPF_RGB, 0, 32, 0xFF0000, 0xFF00, 0xFF, 0x00},
            {sizeof(DDPIXELFORMAT), DDPF_RGB | DDPF_ALPHAPIXELS, 0, 32, 0xFF0000, 0xFF00, 0xFF, 0xFF000000},
            {sizeof(DDPIXELFORMAT), DDPF_FOURCC, FOURCC_DXT1, 0, 0, 0, 0, 0},
            {sizeof(DDPIXELFORMAT), DDPF_FOURCC, FOURCC_DXT3, 0, 0, 0, 0, 0},
            {sizeof(DDPIXELFORMAT), DDPF_FOURCC, FOURCC_DXT5, 0, 0, 0, 0, 0},
        };
        for (auto& f : fmts) cb(&f, ctx);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE BeginScene() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE EndScene() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetDirect3D(IDirect3D7** pp) override { if (pp) *pp = nullptr; return S_OK; }
    HRESULT STDMETHODCALLTYPE SetRenderTarget(LPDIRECTDRAWSURFACE7, DWORD) override { DbgPrint("Device7::SetRenderTarget"); return S_OK; }
    HRESULT STDMETHODCALLTYPE GetRenderTarget(LPDIRECTDRAWSURFACE7* pp) override { if (pp) *pp = nullptr; return S_OK; }
    HRESULT STDMETHODCALLTYPE Clear(DWORD, LPD3DRECT, DWORD, D3DCOLOR, D3DVALUE, DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE SetTransform(D3DTRANSFORMSTATETYPE, LPD3DMATRIX) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetTransform(D3DTRANSFORMSTATETYPE, LPD3DMATRIX) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE SetViewport(LPD3DVIEWPORT7) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE MultiplyTransform(D3DTRANSFORMSTATETYPE, LPD3DMATRIX) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetViewport(LPD3DVIEWPORT7) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE SetMaterial(LPD3DMATERIAL7) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetMaterial(LPD3DMATERIAL7) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE SetLight(DWORD, LPD3DLIGHT7) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetLight(DWORD, LPD3DLIGHT7) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE SetRenderState(D3DRENDERSTATETYPE, DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetRenderState(D3DRENDERSTATETYPE, LPDWORD v) override { if (v) *v = 0; return S_OK; }
    HRESULT STDMETHODCALLTYPE BeginStateBlock() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE EndStateBlock(LPDWORD h) override { if (h) *h = 1; return S_OK; }
    HRESULT STDMETHODCALLTYPE PreLoad(LPDIRECTDRAWSURFACE7) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE DrawPrimitive(D3DPRIMITIVETYPE, DWORD, LPVOID, DWORD, DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE DrawIndexedPrimitive(D3DPRIMITIVETYPE, DWORD, LPVOID, DWORD, LPWORD, DWORD, DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE SetClipStatus(LPD3DCLIPSTATUS) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetClipStatus(LPD3DCLIPSTATUS) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE DrawPrimitiveStrided(D3DPRIMITIVETYPE, DWORD, LPD3DDRAWPRIMITIVESTRIDEDDATA, DWORD, DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE DrawIndexedPrimitiveStrided(D3DPRIMITIVETYPE, DWORD, LPD3DDRAWPRIMITIVESTRIDEDDATA, DWORD, LPWORD, DWORD, DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE DrawPrimitiveVB(D3DPRIMITIVETYPE, LPDIRECT3DVERTEXBUFFER7, DWORD, DWORD, DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE DrawIndexedPrimitiveVB(D3DPRIMITIVETYPE, LPDIRECT3DVERTEXBUFFER7, DWORD, DWORD, LPWORD, DWORD, DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE ComputeSphereVisibility(LPD3DVECTOR, LPD3DVALUE, DWORD, DWORD, LPDWORD ret) override { if (ret) *ret = 0; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetTexture(DWORD, LPDIRECTDRAWSURFACE7* pp) override { if (pp) *pp = nullptr; return S_OK; }
    HRESULT STDMETHODCALLTYPE SetTexture(DWORD, LPDIRECTDRAWSURFACE7) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetTextureStageState(DWORD, D3DTEXTURESTAGESTATETYPE, LPDWORD v) override { if (v) *v = 0; return S_OK; }
    HRESULT STDMETHODCALLTYPE SetTextureStageState(DWORD, D3DTEXTURESTAGESTATETYPE, DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE ValidateDevice(LPDWORD p) override { if (p) *p = 1; return S_OK; }
    HRESULT STDMETHODCALLTYPE ApplyStateBlock(DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE CaptureStateBlock(DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE DeleteStateBlock(DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE CreateStateBlock(D3DSTATEBLOCKTYPE, LPDWORD h) override { if (h) *h = 1; return S_OK; }
    HRESULT STDMETHODCALLTYPE Load(LPDIRECTDRAWSURFACE7, LPPOINT, LPDIRECTDRAWSURFACE7, LPRECT, DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE LightEnable(DWORD, BOOL) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetLightEnable(DWORD, BOOL* b) override { if (b) *b = FALSE; return S_OK; }
    HRESULT STDMETHODCALLTYPE SetClipPlane(DWORD, D3DVALUE*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetClipPlane(DWORD, D3DVALUE*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetInfo(DWORD, LPVOID, DWORD) override { return S_OK; }
};

// ============================================================
// StubDirect3D7
// ============================================================
class StubDirect3D7 : public IDirect3D7 {
    int refCount = 1;
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void** ppv) override { *ppv = this; AddRef(); return S_OK; }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refCount; }
    ULONG STDMETHODCALLTYPE Release() override { if (--refCount == 0) { delete this; return 0; } return refCount; }

    HRESULT STDMETHODCALLTYPE EnumDevices(LPD3DENUMDEVICESCALLBACK7 cb, LPVOID ctx) override {
        DbgPrint("Direct3D7::EnumDevices");
        D3DDEVICEDESC7 devDesc = {};
        devDesc.dwDevCaps = D3DDEVCAPS_HWRASTERIZATION | D3DDEVCAPS_HWTRANSFORMANDLIGHT |
            D3DDEVCAPS_DRAWPRIMTLVERTEX | D3DDEVCAPS_DRAWPRIMITIVES2 | D3DDEVCAPS_DRAWPRIMITIVES2EX;
        devDesc.dpcTriCaps.dwSize = sizeof(D3DPRIMCAPS);
        devDesc.dpcTriCaps.dwTextureCaps = D3DPTEXTURECAPS_PERSPECTIVE | D3DPTEXTURECAPS_ALPHA;
        devDesc.dpcTriCaps.dwTextureFilterCaps = 0x3FFFF;
        devDesc.dpcLineCaps = devDesc.dpcTriCaps;
        devDesc.dwMinTextureWidth = 1;
        devDesc.dwMinTextureHeight = 1;
        devDesc.dwMaxTextureWidth = 16384;
        devDesc.dwMaxTextureHeight = 16384;
        devDesc.dwMaxTextureRepeat = 32768;
        devDesc.dwMaxTextureAspectRatio = 32768;
        devDesc.dwMaxAnisotropy = 16;
        devDesc.wMaxTextureBlendStages = 4;
        devDesc.wMaxSimultaneousTextures = 4;
        devDesc.dwMaxActiveLights = 8;
        devDesc.dvMaxVertexW = 1e10f;
        devDesc.deviceGUID = {0xF5049E78, 0x4861, 0x11D2, {0xA4, 0x07, 0x00, 0xA0, 0xC9, 0x06, 0x29, 0xA8}};
        devDesc.wMaxUserClipPlanes = 6;
        devDesc.wMaxVertexBlendMatrices = 4;
        devDesc.dwVertexProcessingCaps = D3DVTXPCAPS_TEXGEN | D3DVTXPCAPS_MATERIALSOURCE7 | D3DVTXPCAPS_DIRECTIONALLIGHTS | D3DVTXPCAPS_POSITIONALLIGHTS | D3DVTXPCAPS_LOCALVIEWER;
        char desc[] = "GVLK OpenGL Renderer";
        char name[] = "GVLK";
        cb(desc, name, &devDesc, ctx);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE CreateDevice(REFCLSID, LPDIRECTDRAWSURFACE7, LPDIRECT3DDEVICE7* dev) override {
        DbgPrint("Direct3D7::CreateDevice");
        *dev = new StubDirect3DDevice7();
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE CreateVertexBuffer(LPD3DVERTEXBUFFERDESC d, LPDIRECT3DVERTEXBUFFER7* vb, DWORD) override {
        DbgPrint("Direct3D7::CreateVertexBuffer");
        *vb = new StubDirect3DVertexBuffer7(*d);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE EnumZBufferFormats(REFCLSID, LPD3DENUMPIXELFORMATSCALLBACK cb, LPVOID ctx) override {
        DbgPrint("Direct3D7::EnumZBufferFormats");
        DDPIXELFORMAT fmts[] = {
            {sizeof(DDPIXELFORMAT), DDPF_ZBUFFER, 0, 16, 0, 0xFFFF, 0, 0},
            {sizeof(DDPIXELFORMAT), DDPF_ZBUFFER, 0, 24, 0, 0xFFFFFF, 0, 0},
            {sizeof(DDPIXELFORMAT), DDPF_ZBUFFER, 0, 32, 0, 0xFFFFFF, 0, 0},
            {sizeof(DDPIXELFORMAT), DDPF_STENCILBUFFER | DDPF_ZBUFFER, 0, 32, 8, 0xFFFFFF, 0xFF000000, 0},
        };
        for (auto& f : fmts) cb(&f, ctx);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE EvictManagedTextures() override { return S_OK; }
};

// ============================================================
// StubDirectDraw7
// ============================================================
class StubDirectDraw7 : public IDirectDraw7 {
    int refCount = 1;
    DDSURFACEDESC2 displayMode = {};
public:
    StubDirectDraw7() {
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

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        DbgPrint("DirectDraw7::QueryInterface");
        if (riid == IID_IDirect3D7) {
            *ppv = new StubDirect3D7();
            return S_OK;
        }
        *ppv = this;
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refCount; }
    ULONG STDMETHODCALLTYPE Release() override { if (--refCount == 0) { delete this; return 0; } return refCount; }

    HRESULT STDMETHODCALLTYPE Compact() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE CreateClipper(DWORD, LPDIRECTDRAWCLIPPER* c, IUnknown*) override {
        DbgPrint("DirectDraw7::CreateClipper");
        *c = new StubDirectDrawClipper();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE CreatePalette(DWORD, LPPALETTEENTRY, LPDIRECTDRAWPALETTE*, IUnknown*) override { return S_OK; }

    HRESULT STDMETHODCALLTYPE CreateSurface(LPDDSURFACEDESC2 d, LPDIRECTDRAWSURFACE7* s, IUnknown*) override {
        DWORD caps = d ? d->ddsCaps.dwCaps : 0;
        DbgPrint("DirectDraw7::CreateSurface caps=0x%X size=%ux%u", caps, d ? d->dwWidth : 0, d ? d->dwHeight : 0);
        if (d) {
            DbgPrint("  desc flags=0x%X caps2=0x%X pfFlags=0x%X pfBitCount=%u pfZDepth=%u",
                d->dwFlags, d->ddsCaps.dwCaps2,
                d->ddpfPixelFormat.dwFlags,
                d->ddpfPixelFormat.dwRGBBitCount,
                d->ddpfPixelFormat.dwZBufferBitDepth);
        }

        DWORD w = (d && d->dwWidth > 0) ? d->dwWidth : displayMode.dwWidth;
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
            // Dump vtable for diagnostic
            void** vtable = *(void***)zbuf;
            DbgPrint("  -> ZBuffer@0x%p vtable@0x%p [0]=0x%p [1]=0x%p [2]=0x%p [3]=0x%p",
                zbuf, vtable, vtable[0], vtable[1], vtable[2], vtable[3]);
            DbgPrint("  -> ZBuffer %ux%u depth=%u DONE", w, h, d ? d->ddpfPixelFormat.dwZBufferBitDepth : 0);
        } else {
            DWORD tw = (d && d->dwWidth > 0) ? d->dwWidth : 256;
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

    HRESULT STDMETHODCALLTYPE DuplicateSurface(LPDIRECTDRAWSURFACE7, LPDIRECTDRAWSURFACE7* s) override {
        if (s) *s = new StubDirectDrawSurface7("dup");
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE EnumDisplayModes(DWORD flags, LPDDSURFACEDESC2, LPVOID ctx, LPDDENUMMODESCALLBACK2 cb) override {
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
    HRESULT STDMETHODCALLTYPE EnumSurfaces(DWORD, LPDDSURFACEDESC2, LPVOID, LPDDENUMSURFACESCALLBACK7) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE FlipToGDISurface() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetCaps(LPDDCAPS drv, LPDDCAPS hel) override {
        if (drv) { memset(drv, 0, sizeof(DDCAPS)); drv->dwSize = sizeof(DDCAPS); }
        if (hel) { memset(hel, 0, sizeof(DDCAPS)); hel->dwSize = sizeof(DDCAPS); }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetDisplayMode(LPDDSURFACEDESC2 d) override { if (d) *d = displayMode; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetFourCCCodes(LPDWORD n, LPDWORD) override { if (n) *n = 0; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetGDISurface(LPDIRECTDRAWSURFACE7* pp) override { if (pp) *pp = nullptr; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetMonitorFrequency(LPDWORD f) override { if (f) *f = 60; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetScanLine(LPDWORD l) override { if (l) *l = 0; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetVerticalBlankStatus(LPBOOL b) override { if (b) *b = FALSE; return S_OK; }
    HRESULT STDMETHODCALLTYPE Initialize(GUID*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE RestoreDisplayMode() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE SetCooperativeLevel(HWND hwnd, DWORD) override {
        DbgPrint("DirectDraw7::SetCooperativeLevel hwnd=0x%p", hwnd);
        if (hwnd) {
            GVLK_OnSetWindow(hwnd);
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetDisplayMode(DWORD w, DWORD h, DWORD bpp, DWORD refresh, DWORD) override {
        DbgPrint("DirectDraw7::SetDisplayMode %ux%u@%ubpp", w, h, bpp);
        displayMode.dwWidth = w;
        displayMode.dwHeight = h;
        displayMode.dwRefreshRate = refresh;
        displayMode.ddpfPixelFormat.dwRGBBitCount = bpp;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE WaitForVerticalBlank(DWORD, HANDLE) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetAvailableVidMem(LPDDSCAPS2, LPDWORD total, LPDWORD free) override {
        if (total) *total = 512 * 1024 * 1024;
        if (free) *free = 256 * 1024 * 1024;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetSurfaceFromDC(HDC, LPDIRECTDRAWSURFACE7* pp) override { if (pp) *pp = nullptr; return S_OK; }
    HRESULT STDMETHODCALLTYPE RestoreAllSurfaces() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE TestCooperativeLevel() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetDeviceIdentifier(LPDDDEVICEIDENTIFIER2 id, DWORD) override {
        if (id) {
            memset(id, 0, sizeof(DDDEVICEIDENTIFIER2));
            strcpy(id->szDescription, "GVLK OpenGL Renderer");
            strcpy(id->szDriver, "GVLK");
            id->guidDeviceIdentifier = {0xF5049E78, 0x4861, 0x11D2, {0xA4, 0x07, 0x00, 0xA0, 0xC9, 0x06, 0x29, 0xA8}};
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE StartModeTest(LPSIZE, DWORD, DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE EvaluateMode(DWORD, DWORD*) override { return S_OK; }
};
