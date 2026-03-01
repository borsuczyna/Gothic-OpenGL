#include "proxy_device7.h"
#include "proxy_surface7.h"
#include "../debug.h"
#include "../renderer/vk_renderer.h"
#include "../renderer/vk_window.h"
#include <cstring>
#include <cstdio>

static FILE* g_drawLog = nullptr;
static int g_drawLogFrames = 0;
static int g_drawLogCalls = 0;

static void InitDrawLog() {
    if (!g_drawLog) {
        const char* paths[] = {
            "C:\\Users\\libxx\\Desktop\\gvlk_drawlog.txt",
            "gvlk_drawlog.txt",
            nullptr
        };
        for (int i = 0; paths[i]; i++) {
            g_drawLog = fopen(paths[i], "w");
            if (g_drawLog) break;
        }
        g_drawLogFrames = 0;
        g_drawLogCalls = 0;
    }
}

static void LogDrawCall(const char* func, D3DPRIMITIVETYPE type, DWORD fvf,
                        DWORD vertCount, DWORD idxCount,
                        StubDirectDrawSurface7* tex0, StubDirectDrawSurface7* tex1,
                        const unsigned char* vertData, DWORD stride) {
    if (!g_drawLog || g_drawLogFrames > 5) return;
    g_drawLogCalls++;

    DWORD texCount = (fvf & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT;
    fprintf(g_drawLog, "[%d] %s type=%d fvf=0x%08lX stride=%lu verts=%lu idx=%lu texSets=%lu",
            g_drawLogCalls, func, type, fvf, stride, vertCount, idxCount, texCount);

    if (tex0) {
        auto& d = tex0->GetDescRef();
        fprintf(g_drawLog, " tex0=%dx%d", d.dwWidth, d.dwHeight);
        if (d.ddpfPixelFormat.dwFlags & DDPF_FOURCC)
            fprintf(g_drawLog, "(%.4s)", (char*)&d.ddpfPixelFormat.dwFourCC);
        else
            fprintf(g_drawLog, "(%dbpp)", d.ddpfPixelFormat.dwRGBBitCount);
    } else {
        fprintf(g_drawLog, " tex0=NULL");
    }
    if (tex1) {
        auto& d = tex1->GetDescRef();
        fprintf(g_drawLog, " tex1=%dx%d", d.dwWidth, d.dwHeight);
        if (d.ddpfPixelFormat.dwFlags & DDPF_FOURCC)
            fprintf(g_drawLog, "(%.4s)", (char*)&d.ddpfPixelFormat.dwFourCC);
        else
            fprintf(g_drawLog, "(%dbpp)", d.ddpfPixelFormat.dwRGBBitCount);
    } else {
        fprintf(g_drawLog, " tex1=NULL");
    }
    fprintf(g_drawLog, "\n");

    if (texCount >= 2 && vertData && stride > 0) {
        int posSize = 12;
        if ((fvf & D3DFVF_POSITION_MASK) == D3DFVF_XYZRHW) posSize = 16;
        int off = posSize;
        if (fvf & D3DFVF_NORMAL) off += 12;
        if (fvf & D3DFVF_DIFFUSE) off += 4;
        if (fvf & D3DFVF_SPECULAR) off += 4;

        static const int tcSizes[] = {2, 3, 4, 1};
        int uv0Floats = tcSizes[(fvf >> 16) & 3];
        int uv0Off = off;
        int uv1Off = off + uv0Floats * 4;

        int numToLog = vertCount < 4 ? vertCount : 4;
        for (int i = 0; i < numToLog; i++) {
            const unsigned char* v = vertData + i * stride;
            float u0 = *(float*)(v + uv0Off);
            float v0 = (uv0Floats >= 2) ? *(float*)(v + uv0Off + 4) : 0.f;
            float u1 = *(float*)(v + uv1Off);
            int uv1Floats = tcSizes[(fvf >> 18) & 3];
            float v1 = (uv1Floats >= 2) ? *(float*)(v + uv1Off + 4) : 0.f;
            float px = *(float*)(v + 0);
            float py = *(float*)(v + 4);
            float pz = *(float*)(v + 8);
            fprintf(g_drawLog, "  v[%d] pos=(%.1f,%.1f,%.1f) uv0=(%.4f,%.4f) uv1=(%.4f,%.4f)\n",
                    i, px, py, pz, u0, v0, u1, v1);
        }
    }
    fflush(g_drawLog);
}

static void LogFrameEnd() {
    if (g_drawLog && g_drawLogFrames <= 5) {
        g_drawLogFrames++;
        fprintf(g_drawLog, "=== FRAME %d END ===\n", g_drawLogFrames);
        fflush(g_drawLog);
        if (g_drawLogFrames > 5) {
            fclose(g_drawLog);
            g_drawLog = nullptr;
        }
    }
}

static DWORD CalcFVFStride(DWORD fvf) {
    DWORD stride = 0;
    switch (fvf & D3DFVF_POSITION_MASK) {
        case D3DFVF_XYZ:    stride = 12; break;
        case D3DFVF_XYZRHW: stride = 16; break;
        case D3DFVF_XYZB1:  stride = 16; break;
        case D3DFVF_XYZB2:  stride = 20; break;
        case D3DFVF_XYZB3:  stride = 24; break;
        case D3DFVF_XYZB4:  stride = 28; break;
        case D3DFVF_XYZB5:  stride = 32; break;
    }
    if (fvf & D3DFVF_NORMAL)   stride += 12;
    if (fvf & D3DFVF_DIFFUSE)  stride += 4;
    if (fvf & D3DFVF_SPECULAR) stride += 4;
    static const int tcSizes[] = {2, 3, 4, 1};
    DWORD texCount = (fvf & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT;
    for (DWORD i = 0; i < texCount; i++)
        stride += tcSizes[(fvf >> (i * 2 + 16)) & 3] * 4;
    return stride;
}

void StubDirect3DDevice7::EnsureContext() {
    InitDrawLog();
    if (!contextAcquired) {
        if (GVulkan_IsReady()) {
            VkRenderer::Init();
            contextAcquired = true;
            DbgPrint("Device7: Vulkan renderer initialized on game thread");
        }
    }
}

void StubDirect3DDevice7::InitIdentityMatrix(D3DMATRIX& m) {
    memset(&m, 0, sizeof(D3DMATRIX));
    m._11 = m._22 = m._33 = m._44 = 1.0f;
}

StubDirect3DDevice7::StubDirect3DDevice7() {
    InitIdentityMatrix(worldMatrix);
    InitIdentityMatrix(viewMatrix);
    InitIdentityMatrix(projMatrix);
    memset(&viewport, 0, sizeof(viewport));
    memset(&fakeDesc, 0, sizeof(fakeDesc));

    fakeDesc.dwDevCaps = D3DDEVCAPS_FLOATTLVERTEX | D3DDEVCAPS_EXECUTESYSTEMMEMORY |
        D3DDEVCAPS_TLVERTEXSYSTEMMEMORY | D3DDEVCAPS_TEXTUREVIDEOMEMORY |
        D3DDEVCAPS_DRAWPRIMTLVERTEX | D3DDEVCAPS_CANRENDERAFTERFLIP |
        D3DDEVCAPS_DRAWPRIMITIVES2 | D3DDEVCAPS_DRAWPRIMITIVES2EX |
        D3DDEVCAPS_HWTRANSFORMANDLIGHT | D3DDEVCAPS_HWRASTERIZATION;

    fakeDesc.dpcTriCaps.dwSize = sizeof(D3DPRIMCAPS);
    fakeDesc.dpcTriCaps.dwMiscCaps    = D3DPMISCCAPS_MASKZ | D3DPMISCCAPS_CULLNONE | D3DPMISCCAPS_CULLCW | D3DPMISCCAPS_CULLCCW;
    fakeDesc.dpcTriCaps.dwRasterCaps  = D3DPRASTERCAPS_DITHER | D3DPRASTERCAPS_ZTEST | D3DPRASTERCAPS_FOGVERTEX | D3DPRASTERCAPS_FOGTABLE | D3DPRASTERCAPS_ANISOTROPY;
    fakeDesc.dpcTriCaps.dwZCmpCaps    = 0xFF;
    fakeDesc.dpcTriCaps.dwSrcBlendCaps  = 0x1FFF;
    fakeDesc.dpcTriCaps.dwDestBlendCaps = 0x1FFF;
    fakeDesc.dpcTriCaps.dwAlphaCmpCaps  = 0xFF;
    fakeDesc.dpcTriCaps.dwShadeCaps     = 0xFF;
    fakeDesc.dpcTriCaps.dwTextureCaps   = D3DPTEXTURECAPS_PERSPECTIVE | D3DPTEXTURECAPS_ALPHA | D3DPTEXTURECAPS_TRANSPARENCY;
    fakeDesc.dpcTriCaps.dwTextureFilterCaps  = 0x3FFFF;
    fakeDesc.dpcTriCaps.dwTextureBlendCaps   = 0xFF;
    fakeDesc.dpcTriCaps.dwTextureAddressCaps = 0x1F;
    fakeDesc.dpcLineCaps = fakeDesc.dpcTriCaps;

    fakeDesc.dwDeviceRenderBitDepth  = 1280;
    fakeDesc.dwDeviceZBufferBitDepth = 1536;
    fakeDesc.dwMinTextureWidth  = 1;
    fakeDesc.dwMinTextureHeight = 1;
    fakeDesc.dwMaxTextureWidth  = 16384;
    fakeDesc.dwMaxTextureHeight = 16384;
    fakeDesc.dwMaxTextureRepeat      = 32768;
    fakeDesc.dwMaxTextureAspectRatio = 32768;
    fakeDesc.dwMaxAnisotropy = 16;
    fakeDesc.wMaxTextureBlendStages   = 4;
    fakeDesc.wMaxSimultaneousTextures = 4;
    fakeDesc.dwMaxActiveLights = 8;
    fakeDesc.dvMaxVertexW = 1e10f;
    fakeDesc.deviceGUID = {0xF5049E78, 0x4861, 0x11D2, {0xA4, 0x07, 0x00, 0xA0, 0xC9, 0x06, 0x29, 0xA8}};
    fakeDesc.wMaxUserClipPlanes      = 6;
    fakeDesc.wMaxVertexBlendMatrices = 4;
    fakeDesc.dwVertexProcessingCaps  = D3DVTXPCAPS_TEXGEN | D3DVTXPCAPS_MATERIALSOURCE7 | D3DVTXPCAPS_DIRECTIONALLIGHTS | D3DVTXPCAPS_POSITIONALLIGHTS | D3DVTXPCAPS_LOCALVIEWER;
    fakeDesc.dwStencilCaps     = 0xFF;
    fakeDesc.dwFVFCaps         = D3DFVFCAPS_DONOTSTRIPELEMENTS | 8;
    fakeDesc.dwTextureOpCaps   = 0x3FFFFFF;
}

HRESULT STDMETHODCALLTYPE StubDirect3DDevice7::QueryInterface(REFIID, void** ppv) { DbgPrint("Device7::QI"); *ppv = this; AddRef(); return S_OK; }
ULONG   STDMETHODCALLTYPE StubDirect3DDevice7::AddRef() { return ++refCount; }
ULONG   STDMETHODCALLTYPE StubDirect3DDevice7::Release() { if (--refCount == 0) { delete this; return 0; } return refCount; }

HRESULT STDMETHODCALLTYPE StubDirect3DDevice7::GetCaps(LPD3DDEVICEDESC7 d) { DbgPrint("Device7::GetCaps"); if (d) *d = fakeDesc; return S_OK; }

HRESULT STDMETHODCALLTYPE StubDirect3DDevice7::EnumTextureFormats(LPD3DENUMPIXELFORMATSCALLBACK cb, LPVOID ctx) {
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

HRESULT STDMETHODCALLTYPE StubDirect3DDevice7::BeginScene() {
    EnsureContext();
    GVulkan_ResetPresentFlag();
    if (contextAcquired) {
        int ww = GVulkan_GetWindowWidth();
        int wh = GVulkan_GetWindowHeight();
        int gw = GVulkan_GetGameWidth();
        int gh = GVulkan_GetGameHeight();
        VkRenderer::BeginFrame(ww, wh, gw, gh);
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE StubDirect3DDevice7::EndScene() {
    LogFrameEnd();
    return S_OK;
}

HRESULT STDMETHODCALLTYPE StubDirect3DDevice7::GetDirect3D(IDirect3D7** pp) { if (pp) *pp = nullptr; return S_OK; }
HRESULT STDMETHODCALLTYPE StubDirect3DDevice7::SetRenderTarget(LPDIRECTDRAWSURFACE7, DWORD) { return S_OK; }
HRESULT STDMETHODCALLTYPE StubDirect3DDevice7::GetRenderTarget(LPDIRECTDRAWSURFACE7* pp) { if (pp) *pp = nullptr; return S_OK; }

HRESULT STDMETHODCALLTYPE StubDirect3DDevice7::Clear(DWORD count, LPD3DRECT rects, DWORD flags, D3DCOLOR color, D3DVALUE z, DWORD stencil) {
    EnsureContext();
    if (contextAcquired) {
        VkRenderer::Clear(flags, color, z, stencil);
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE StubDirect3DDevice7::SetTransform(D3DTRANSFORMSTATETYPE type, LPD3DMATRIX m) {
    if (!m) return S_OK;
    switch (type) {
    case D3DTRANSFORMSTATE_WORLD:
        worldMatrix = *m;
        if (contextAcquired) VkRenderer::SetWorldMatrix((const float*)&worldMatrix);
        break;
    case D3DTRANSFORMSTATE_VIEW:
        viewMatrix = *m;
        if (contextAcquired) VkRenderer::SetViewMatrix((const float*)&viewMatrix);
        break;
    case D3DTRANSFORMSTATE_PROJECTION:
        projMatrix = *m;
        if (contextAcquired) VkRenderer::SetProjectionMatrix((const float*)&projMatrix);
        break;
    default:
        break;
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE StubDirect3DDevice7::GetTransform(D3DTRANSFORMSTATETYPE type, LPD3DMATRIX m) {
    if (!m) return S_OK;
    switch (type) {
    case D3DTRANSFORMSTATE_WORLD:      *m = worldMatrix; break;
    case D3DTRANSFORMSTATE_VIEW:       *m = viewMatrix; break;
    case D3DTRANSFORMSTATE_PROJECTION: *m = projMatrix; break;
    default: break;
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE StubDirect3DDevice7::SetViewport(LPD3DVIEWPORT7 vp) {
    if (!vp) return S_OK;
    viewport = *vp;
    if (contextAcquired) {
        VkRenderer::SetViewport(viewport.dwX, viewport.dwY, viewport.dwWidth, viewport.dwHeight);
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE StubDirect3DDevice7::MultiplyTransform(D3DTRANSFORMSTATETYPE, LPD3DMATRIX) { return S_OK; }

HRESULT STDMETHODCALLTYPE StubDirect3DDevice7::GetViewport(LPD3DVIEWPORT7 vp) {
    if (vp) *vp = viewport;
    return S_OK;
}
HRESULT STDMETHODCALLTYPE StubDirect3DDevice7::SetMaterial(LPD3DMATERIAL7) { return S_OK; }
HRESULT STDMETHODCALLTYPE StubDirect3DDevice7::GetMaterial(LPD3DMATERIAL7) { return S_OK; }
HRESULT STDMETHODCALLTYPE StubDirect3DDevice7::SetLight(DWORD, LPD3DLIGHT7) { return S_OK; }
HRESULT STDMETHODCALLTYPE StubDirect3DDevice7::GetLight(DWORD, LPD3DLIGHT7) { return S_OK; }

HRESULT STDMETHODCALLTYPE StubDirect3DDevice7::SetRenderState(D3DRENDERSTATETYPE state, DWORD value) {
    switch (state) {
    case D3DRENDERSTATE_ALPHABLENDENABLE:
        alphaBlendEnabled = (value != 0);
        if (contextAcquired) VkRenderer::SetAlphaBlendEnabled(alphaBlendEnabled);
        break;
    case D3DRENDERSTATE_SRCBLEND:
        srcBlend = value;
        if (contextAcquired) VkRenderer::SetBlendFunc(srcBlend, dstBlend);
        break;
    case D3DRENDERSTATE_DESTBLEND:
        dstBlend = value;
        if (contextAcquired) VkRenderer::SetBlendFunc(srcBlend, dstBlend);
        break;
    case D3DRENDERSTATE_ALPHATESTENABLE:
        alphaTestEnabled = (value != 0);
        if (contextAcquired) VkRenderer::SetAlphaTestEnabled(alphaTestEnabled);
        break;
    case D3DRENDERSTATE_ALPHAREF:
        alphaRef = value;
        if (contextAcquired) VkRenderer::SetAlphaRef(alphaRef);
        break;
    case D3DRENDERSTATE_ZENABLE:
        zEnabled = (value != 0);
        if (contextAcquired) VkRenderer::SetDepthEnabled(zEnabled);
        break;
    case D3DRENDERSTATE_ZWRITEENABLE:
        zWriteEnabled = (value != 0);
        if (contextAcquired) VkRenderer::SetDepthWriteEnabled(zWriteEnabled);
        break;
    case D3DRENDERSTATE_ZFUNC:
        zFunc = value;
        if (contextAcquired) VkRenderer::SetDepthFunc(zFunc);
        break;
    default:
        break;
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE StubDirect3DDevice7::GetRenderState(D3DRENDERSTATETYPE, LPDWORD v) { if (v) *v = 0; return S_OK; }
HRESULT STDMETHODCALLTYPE StubDirect3DDevice7::BeginStateBlock() { return S_OK; }
HRESULT STDMETHODCALLTYPE StubDirect3DDevice7::EndStateBlock(LPDWORD h) { if (h) *h = 1; return S_OK; }
HRESULT STDMETHODCALLTYPE StubDirect3DDevice7::PreLoad(LPDIRECTDRAWSURFACE7) { return S_OK; }

HRESULT STDMETHODCALLTYPE StubDirect3DDevice7::DrawPrimitive(D3DPRIMITIVETYPE type, DWORD fvf, LPVOID verts, DWORD count, DWORD) {
    if (!contextAcquired || !verts || count == 0) return S_OK;

    if (boundTextures[0]) {
        if (boundTextures[0]->IsTextureDirty()) boundTextures[0]->UploadTextureToVk();
        VkRenderer::BindTexture(boundTextures[0]->GetVkTexture());
    } else {
        VkRenderer::BindTexture(nullptr);
    }
    if (boundTextures[1]) {
        if (boundTextures[1]->IsTextureDirty()) boundTextures[1]->UploadTextureToVk();
        VkRenderer::BindTexture2(boundTextures[1]->GetVkTexture());
    } else {
        VkRenderer::BindTexture2(nullptr);
    }

    LogDrawCall("DrawPrim", type, fvf, count, 0, boundTextures[0], boundTextures[1],
                (const unsigned char*)verts, CalcFVFStride(fvf));
    VkRenderer::DrawPrimitive(type, fvf, verts, count);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE StubDirect3DDevice7::DrawIndexedPrimitive(D3DPRIMITIVETYPE type, DWORD fvf, LPVOID verts, DWORD vertCount, LPWORD indices, DWORD idxCount, DWORD) {
    if (!contextAcquired || !verts || !indices || idxCount == 0) return S_OK;

    if (boundTextures[0]) {
        if (boundTextures[0]->IsTextureDirty()) boundTextures[0]->UploadTextureToVk();
        VkRenderer::BindTexture(boundTextures[0]->GetVkTexture());
    } else {
        VkRenderer::BindTexture(nullptr);
    }
    if (boundTextures[1]) {
        if (boundTextures[1]->IsTextureDirty()) boundTextures[1]->UploadTextureToVk();
        VkRenderer::BindTexture2(boundTextures[1]->GetVkTexture());
    } else {
        VkRenderer::BindTexture2(nullptr);
    }

    LogDrawCall("DrawIdxPrim", type, fvf, vertCount, idxCount, boundTextures[0], boundTextures[1],
                (const unsigned char*)verts, CalcFVFStride(fvf));
    VkRenderer::DrawIndexedPrimitive(type, fvf, verts, vertCount, indices, idxCount);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE StubDirect3DDevice7::SetClipStatus(LPD3DCLIPSTATUS) { return S_OK; }
HRESULT STDMETHODCALLTYPE StubDirect3DDevice7::GetClipStatus(LPD3DCLIPSTATUS) { return S_OK; }
HRESULT STDMETHODCALLTYPE StubDirect3DDevice7::DrawPrimitiveStrided(D3DPRIMITIVETYPE, DWORD, LPD3DDRAWPRIMITIVESTRIDEDDATA, DWORD, DWORD) { return S_OK; }
HRESULT STDMETHODCALLTYPE StubDirect3DDevice7::DrawIndexedPrimitiveStrided(D3DPRIMITIVETYPE, DWORD, LPD3DDRAWPRIMITIVESTRIDEDDATA, DWORD, LPWORD, DWORD, DWORD) { return S_OK; }

HRESULT STDMETHODCALLTYPE StubDirect3DDevice7::DrawPrimitiveVB(D3DPRIMITIVETYPE type, LPDIRECT3DVERTEXBUFFER7 vb, DWORD startVertex, DWORD numVertices, DWORD flags) {
    if (!contextAcquired || !vb || numVertices == 0) return S_OK;

    D3DVERTEXBUFFERDESC vbDesc = {};
    vb->GetVertexBufferDesc(&vbDesc);
    DWORD fvf = vbDesc.dwFVF;

    LPVOID data = nullptr;
    vb->Lock(0, &data, nullptr);
    if (!data) return S_OK;

    DWORD stride = CalcFVFStride(fvf);
    const unsigned char* verts = (const unsigned char*)data + startVertex * stride;

    if (boundTextures[0]) {
        if (boundTextures[0]->IsTextureDirty()) boundTextures[0]->UploadTextureToVk();
        VkRenderer::BindTexture(boundTextures[0]->GetVkTexture());
    } else {
        VkRenderer::BindTexture(nullptr);
    }
    if (boundTextures[1]) {
        if (boundTextures[1]->IsTextureDirty()) boundTextures[1]->UploadTextureToVk();
        VkRenderer::BindTexture2(boundTextures[1]->GetVkTexture());
    } else {
        VkRenderer::BindTexture2(nullptr);
    }

    LogDrawCall("DrawPrimVB", type, fvf, numVertices, 0, boundTextures[0], boundTextures[1], verts, stride);
    VkRenderer::DrawPrimitive(type, fvf, verts, numVertices);

    vb->Unlock();
    return S_OK;
}

HRESULT STDMETHODCALLTYPE StubDirect3DDevice7::DrawIndexedPrimitiveVB(D3DPRIMITIVETYPE type, LPDIRECT3DVERTEXBUFFER7 vb, DWORD startVertex, DWORD numVertices, LPWORD indices, DWORD idxCount, DWORD) {
    if (!contextAcquired || !vb || !indices || idxCount == 0) return S_OK;

    D3DVERTEXBUFFERDESC vbDesc = {};
    vb->GetVertexBufferDesc(&vbDesc);
    DWORD fvf = vbDesc.dwFVF;

    LPVOID data = nullptr;
    vb->Lock(0, &data, nullptr);
    if (!data) return S_OK;

    DWORD stride = CalcFVFStride(fvf);
    const unsigned char* verts = (const unsigned char*)data + startVertex * stride;

    if (boundTextures[0]) {
        if (boundTextures[0]->IsTextureDirty()) boundTextures[0]->UploadTextureToVk();
        VkRenderer::BindTexture(boundTextures[0]->GetVkTexture());
    } else {
        VkRenderer::BindTexture(nullptr);
    }
    if (boundTextures[1]) {
        if (boundTextures[1]->IsTextureDirty()) boundTextures[1]->UploadTextureToVk();
        VkRenderer::BindTexture2(boundTextures[1]->GetVkTexture());
    } else {
        VkRenderer::BindTexture2(nullptr);
    }

    LogDrawCall("DrawIdxPrimVB", type, fvf, numVertices, idxCount, boundTextures[0], boundTextures[1], verts, stride);
    VkRenderer::DrawIndexedPrimitive(type, fvf, verts, numVertices, indices, idxCount);

    vb->Unlock();
    return S_OK;
}

HRESULT STDMETHODCALLTYPE StubDirect3DDevice7::ComputeSphereVisibility(LPD3DVECTOR, LPD3DVALUE, DWORD, DWORD, LPDWORD ret) { if (ret) *ret = 0; return S_OK; }

HRESULT STDMETHODCALLTYPE StubDirect3DDevice7::GetTexture(DWORD stage, LPDIRECTDRAWSURFACE7* pp) {
    if (pp) *pp = (stage < 8) ? (LPDIRECTDRAWSURFACE7)boundTextures[stage] : nullptr;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE StubDirect3DDevice7::SetTexture(DWORD stage, LPDIRECTDRAWSURFACE7 tex) {
    if (stage >= 8) return S_OK;
    boundTextures[stage] = static_cast<StubDirectDrawSurface7*>(tex);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE StubDirect3DDevice7::GetTextureStageState(DWORD, D3DTEXTURESTAGESTATETYPE, LPDWORD v) { if (v) *v = 0; return S_OK; }

HRESULT STDMETHODCALLTYPE StubDirect3DDevice7::SetTextureStageState(DWORD stage, D3DTEXTURESTAGESTATETYPE type, DWORD value) {
    if (!contextAcquired) return S_OK;
    if (g_drawLog && g_drawLogFrames <= 5 && stage < 2 &&
        (type == D3DTSS_COLOROP || type == D3DTSS_COLORARG1 || type == D3DTSS_COLORARG2 ||
         type == D3DTSS_ALPHAOP || type == D3DTSS_TEXCOORDINDEX)) {
        const char* names[] = {"COLOROP","COLORARG1","COLORARG2","ALPHAOP","","","","","","","","TEXCOORDINDEX"};
        int idx = (type <= 3) ? type - 1 : (type == 11 ? 11 : -1);
        fprintf(g_drawLog, "  TSS stage=%lu %s(%d)=%lu\n", stage,
                (idx >= 0 && idx < 12) ? names[idx] : "?", type, value);
        fflush(g_drawLog);
    }
    if (stage < 2) {
        switch (type) {
        case D3DTSS_COLOROP:
            VkRenderer::SetStageColorOp(stage, value);
            break;
        case D3DTSS_COLORARG1:
            VkRenderer::SetStageColorArg(stage, 1, value);
            break;
        case D3DTSS_COLORARG2:
            VkRenderer::SetStageColorArg(stage, 2, value);
            break;
        case D3DTSS_TEXCOORDINDEX:
            VkRenderer::SetStageTexCoordIndex(stage, value);
            break;
        default:
            break;
        }
    }
    if (stage == 0) {
        switch (type) {
        case D3DTSS_ADDRESS:
            VkRenderer::SetTextureAddressU(value);
            VkRenderer::SetTextureAddressV(value);
            break;
        case D3DTSS_ADDRESSU:
            VkRenderer::SetTextureAddressU(value);
            break;
        case D3DTSS_ADDRESSV:
            VkRenderer::SetTextureAddressV(value);
            break;
        default:
            break;
        }
    } else if (stage == 1) {
        switch (type) {
        case D3DTSS_ADDRESS:
            VkRenderer::SetTextureAddress2U(value);
            VkRenderer::SetTextureAddress2V(value);
            break;
        case D3DTSS_ADDRESSU:
            VkRenderer::SetTextureAddress2U(value);
            break;
        case D3DTSS_ADDRESSV:
            VkRenderer::SetTextureAddress2V(value);
            break;
        default:
            break;
        }
    }
    return S_OK;
}
HRESULT STDMETHODCALLTYPE StubDirect3DDevice7::ValidateDevice(LPDWORD p) { if (p) *p = 1; return S_OK; }
HRESULT STDMETHODCALLTYPE StubDirect3DDevice7::ApplyStateBlock(DWORD) { return S_OK; }
HRESULT STDMETHODCALLTYPE StubDirect3DDevice7::CaptureStateBlock(DWORD) { return S_OK; }
HRESULT STDMETHODCALLTYPE StubDirect3DDevice7::DeleteStateBlock(DWORD) { return S_OK; }
HRESULT STDMETHODCALLTYPE StubDirect3DDevice7::CreateStateBlock(D3DSTATEBLOCKTYPE, LPDWORD h) { if (h) *h = 1; return S_OK; }
HRESULT STDMETHODCALLTYPE StubDirect3DDevice7::Load(LPDIRECTDRAWSURFACE7, LPPOINT, LPDIRECTDRAWSURFACE7, LPRECT, DWORD) { return S_OK; }
HRESULT STDMETHODCALLTYPE StubDirect3DDevice7::LightEnable(DWORD, BOOL) { return S_OK; }
HRESULT STDMETHODCALLTYPE StubDirect3DDevice7::GetLightEnable(DWORD, BOOL* b) { if (b) *b = FALSE; return S_OK; }
HRESULT STDMETHODCALLTYPE StubDirect3DDevice7::SetClipPlane(DWORD, D3DVALUE*) { return S_OK; }
HRESULT STDMETHODCALLTYPE StubDirect3DDevice7::GetClipPlane(DWORD, D3DVALUE*) { return S_OK; }
HRESULT STDMETHODCALLTYPE StubDirect3DDevice7::GetInfo(DWORD, LPVOID, DWORD) { return S_OK; }
