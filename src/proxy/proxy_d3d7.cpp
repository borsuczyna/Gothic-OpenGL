#include "proxy_d3d7.h"
#include "proxy_device7.h"
#include "proxy_vertexbuffer7.h"
#include "../debug.h"

HRESULT STDMETHODCALLTYPE StubDirect3D7::QueryInterface(REFIID, void** ppv) { *ppv = this; AddRef(); return S_OK; }
ULONG   STDMETHODCALLTYPE StubDirect3D7::AddRef() { return ++refCount; }
ULONG   STDMETHODCALLTYPE StubDirect3D7::Release() { if (--refCount == 0) { delete this; return 0; } return refCount; }

HRESULT STDMETHODCALLTYPE StubDirect3D7::EnumDevices(LPD3DENUMDEVICESCALLBACK7 cb, LPVOID ctx) {
    DbgPrint("Direct3D7::EnumDevices");
    D3DDEVICEDESC7 devDesc = {};
    devDesc.dwDevCaps = D3DDEVCAPS_HWRASTERIZATION | D3DDEVCAPS_HWTRANSFORMANDLIGHT |
        D3DDEVCAPS_DRAWPRIMTLVERTEX | D3DDEVCAPS_DRAWPRIMITIVES2 | D3DDEVCAPS_DRAWPRIMITIVES2EX;
    devDesc.dpcTriCaps.dwSize = sizeof(D3DPRIMCAPS);
    devDesc.dpcTriCaps.dwTextureCaps = D3DPTEXTURECAPS_PERSPECTIVE | D3DPTEXTURECAPS_ALPHA;
    devDesc.dpcTriCaps.dwTextureFilterCaps = 0x3FFFF;
    devDesc.dpcLineCaps = devDesc.dpcTriCaps;
    devDesc.dwMinTextureWidth  = 1;
    devDesc.dwMinTextureHeight = 1;
    devDesc.dwMaxTextureWidth  = 16384;
    devDesc.dwMaxTextureHeight = 16384;
    devDesc.dwMaxTextureRepeat      = 32768;
    devDesc.dwMaxTextureAspectRatio = 32768;
    devDesc.dwMaxAnisotropy = 16;
    devDesc.wMaxTextureBlendStages   = 4;
    devDesc.wMaxSimultaneousTextures = 4;
    devDesc.dwMaxActiveLights = 8;
    devDesc.dvMaxVertexW = 1e10f;
    devDesc.deviceGUID = {0xF5049E78, 0x4861, 0x11D2, {0xA4, 0x07, 0x00, 0xA0, 0xC9, 0x06, 0x29, 0xA8}};
    devDesc.wMaxUserClipPlanes      = 6;
    devDesc.wMaxVertexBlendMatrices = 4;
    devDesc.dwVertexProcessingCaps  = D3DVTXPCAPS_TEXGEN | D3DVTXPCAPS_MATERIALSOURCE7 | D3DVTXPCAPS_DIRECTIONALLIGHTS | D3DVTXPCAPS_POSITIONALLIGHTS | D3DVTXPCAPS_LOCALVIEWER;
    char desc[] = "GOpenGL Renderer";
    char name[] = "GOpenGL";
    cb(desc, name, &devDesc, ctx);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE StubDirect3D7::CreateDevice(REFCLSID, LPDIRECTDRAWSURFACE7, LPDIRECT3DDEVICE7* dev) {
    DbgPrint("Direct3D7::CreateDevice");
    *dev = new StubDirect3DDevice7();
    return S_OK;
}

HRESULT STDMETHODCALLTYPE StubDirect3D7::CreateVertexBuffer(LPD3DVERTEXBUFFERDESC d, LPDIRECT3DVERTEXBUFFER7* vb, DWORD) {
    DbgPrint("Direct3D7::CreateVertexBuffer");
    *vb = new StubDirect3DVertexBuffer7(*d);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE StubDirect3D7::EnumZBufferFormats(REFCLSID, LPD3DENUMPIXELFORMATSCALLBACK cb, LPVOID ctx) {
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

HRESULT STDMETHODCALLTYPE StubDirect3D7::EvictManagedTextures() { return S_OK; }
