#pragma once

#include <ddraw.h>
#include <vector>

void GOpenGL_OnSetWindow(HWND hwnd);
void GOpenGL_OnPresent();

class StubDirectDrawSurface7 : public IDirectDrawSurface7 {
    int refCount = 1;
    DDSURFACEDESC2 desc;
    StubDirectDrawSurface7* attached = nullptr;
    std::vector<unsigned char> surfaceData;
    const char* tag = "unknown";

    unsigned int glTextureId = 0;
    bool textureDirty = false;

    void EnsureSurfaceBuffer();

public:
    bool isPrimary = false;
    explicit StubDirectDrawSurface7(const char* surfaceTag = "surface");
    ~StubDirectDrawSurface7();

    void SetDesc(LPDDSURFACEDESC2 d);
    void InitAsRGB(DWORD w, DWORD h, DWORD bpp);
    void InitAsZBuffer(DWORD w, DWORD h, const DDPIXELFORMAT& fmt);
    void InitAsFourCC(DWORD w, DWORD h, DWORD fourCC);
    void AttachBackBuffer(StubDirectDrawSurface7* bb);

    void UploadTextureToGL();
    unsigned int GetGLTextureId() const { return glTextureId; }
    bool IsTextureDirty() const { return textureDirty; }
    const DDSURFACEDESC2& GetDescRef() const { return desc; }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void** ppv) override;
    ULONG   STDMETHODCALLTYPE AddRef() override;
    ULONG   STDMETHODCALLTYPE Release() override;

    HRESULT STDMETHODCALLTYPE AddAttachedSurface(LPDIRECTDRAWSURFACE7) override;
    HRESULT STDMETHODCALLTYPE AddOverlayDirtyRect(LPRECT) override;
    HRESULT STDMETHODCALLTYPE Blt(LPRECT, LPDIRECTDRAWSURFACE7, LPRECT, DWORD, LPDDBLTFX) override;
    HRESULT STDMETHODCALLTYPE BltBatch(LPDDBLTBATCH, DWORD, DWORD) override;
    HRESULT STDMETHODCALLTYPE BltFast(DWORD, DWORD, LPDIRECTDRAWSURFACE7, LPRECT, DWORD) override;
    HRESULT STDMETHODCALLTYPE DeleteAttachedSurface(DWORD, LPDIRECTDRAWSURFACE7) override;
    HRESULT STDMETHODCALLTYPE EnumAttachedSurfaces(LPVOID, LPDDENUMSURFACESCALLBACK7) override;
    HRESULT STDMETHODCALLTYPE EnumOverlayZOrders(DWORD, LPVOID, LPDDENUMSURFACESCALLBACK7) override;
    HRESULT STDMETHODCALLTYPE Flip(LPDIRECTDRAWSURFACE7, DWORD) override;
    HRESULT STDMETHODCALLTYPE GetAttachedSurface(LPDDSCAPS2, LPDIRECTDRAWSURFACE7*) override;
    HRESULT STDMETHODCALLTYPE GetBltStatus(DWORD) override;
    HRESULT STDMETHODCALLTYPE GetCaps(LPDDSCAPS2) override;
    HRESULT STDMETHODCALLTYPE GetClipper(LPDIRECTDRAWCLIPPER*) override;
    HRESULT STDMETHODCALLTYPE GetColorKey(DWORD, LPDDCOLORKEY) override;
    HRESULT STDMETHODCALLTYPE GetDC(HDC*) override;
    HRESULT STDMETHODCALLTYPE GetFlipStatus(DWORD) override;
    HRESULT STDMETHODCALLTYPE GetOverlayPosition(LPLONG, LPLONG) override;
    HRESULT STDMETHODCALLTYPE GetPalette(LPDIRECTDRAWPALETTE*) override;
    HRESULT STDMETHODCALLTYPE GetPixelFormat(LPDDPIXELFORMAT) override;
    HRESULT STDMETHODCALLTYPE GetSurfaceDesc(LPDDSURFACEDESC2) override;
    HRESULT STDMETHODCALLTYPE Initialize(LPDIRECTDRAW, LPDDSURFACEDESC2) override;
    HRESULT STDMETHODCALLTYPE IsLost() override;
    HRESULT STDMETHODCALLTYPE Lock(LPRECT, LPDDSURFACEDESC2, DWORD, HANDLE) override;
    HRESULT STDMETHODCALLTYPE ReleaseDC(HDC) override;
    HRESULT STDMETHODCALLTYPE Restore() override;
    HRESULT STDMETHODCALLTYPE SetClipper(LPDIRECTDRAWCLIPPER) override;
    HRESULT STDMETHODCALLTYPE SetColorKey(DWORD, LPDDCOLORKEY) override;
    HRESULT STDMETHODCALLTYPE SetOverlayPosition(LONG, LONG) override;
    HRESULT STDMETHODCALLTYPE SetPalette(LPDIRECTDRAWPALETTE) override;
    HRESULT STDMETHODCALLTYPE Unlock(LPRECT) override;
    HRESULT STDMETHODCALLTYPE UpdateOverlay(LPRECT, LPDIRECTDRAWSURFACE7, LPRECT, DWORD, LPDDOVERLAYFX) override;
    HRESULT STDMETHODCALLTYPE UpdateOverlayDisplay(DWORD) override;
    HRESULT STDMETHODCALLTYPE UpdateOverlayZOrder(DWORD, LPDIRECTDRAWSURFACE7) override;
    HRESULT STDMETHODCALLTYPE GetDDInterface(LPVOID*) override;
    HRESULT STDMETHODCALLTYPE PageLock(DWORD) override;
    HRESULT STDMETHODCALLTYPE PageUnlock(DWORD) override;
    HRESULT STDMETHODCALLTYPE SetSurfaceDesc(LPDDSURFACEDESC2, DWORD) override;
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID, LPVOID, DWORD, DWORD) override;
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID, LPVOID, LPDWORD) override;
    HRESULT STDMETHODCALLTYPE FreePrivateData(REFGUID) override;
    HRESULT STDMETHODCALLTYPE GetUniquenessValue(LPDWORD) override;
    HRESULT STDMETHODCALLTYPE ChangeUniquenessValue() override;
    HRESULT STDMETHODCALLTYPE SetPriority(DWORD) override;
    HRESULT STDMETHODCALLTYPE GetPriority(LPDWORD) override;
    HRESULT STDMETHODCALLTYPE SetLOD(DWORD) override;
    HRESULT STDMETHODCALLTYPE GetLOD(LPDWORD) override;
};
