#include "ProxyDevice7.h"
#include "ProxySurface7.h"
#include "../Debug.h"
#include "../renderer/VkRenderer.h"
#include "../renderer/VkWindow.h"
#include "../renderer/WorldReconstructor.h"
#include <cstring>

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
    if (!contextAcquired) {
        if (GVulkan_IsReady()) {
            VkRenderer::Init();
            contextAcquired = true;
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

HRESULT STDMETHODCALLTYPE StubDirect3DDevice7::QueryInterface(REFIID, void** ppv) { *ppv = this; AddRef(); return S_OK; }
ULONG   STDMETHODCALLTYPE StubDirect3DDevice7::AddRef() { return ++refCount; }
ULONG   STDMETHODCALLTYPE StubDirect3DDevice7::Release() { if (--refCount == 0) { delete this; return 0; } return refCount; }

HRESULT STDMETHODCALLTYPE StubDirect3DDevice7::GetCaps(LPD3DDEVICEDESC7 d) { if (d) *d = fakeDesc; return S_OK; }

HRESULT STDMETHODCALLTYPE StubDirect3DDevice7::EnumTextureFormats(LPD3DENUMPIXELFORMATSCALLBACK cb, LPVOID ctx) {
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
    WorldReconstructor::PrintIfReady();
    WorldReconstructor::BeginFrame();
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
    if (contextAcquired) {
        VkRenderer::DrawReconstructedWorld();
    }
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
    case D3DRENDERSTATE_TEXTUREFACTOR:
        textureFactor = value;
        if (contextAcquired) VkRenderer::SetTextureFactor(textureFactor);
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
    if ((fvf & D3DFVF_POSITION_MASK) == D3DFVF_XYZRHW) {
        VkRenderer::DrawPrimitive(type, fvf, verts, count);
    } else {
        VkTexHandle* tex = nullptr;
        if (boundTextures[0]) {
            if (boundTextures[0]->IsTextureDirty()) boundTextures[0]->UploadTextureToVk();
            tex = boundTextures[0]->GetVkTexture();
        }
        WorldReconstructor::CaptureDrawCall(type, fvf, verts, count, nullptr, 0,
            (const float*)&worldMatrix, tex);
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE StubDirect3DDevice7::DrawIndexedPrimitive(D3DPRIMITIVETYPE type, DWORD fvf, LPVOID verts, DWORD vertCount, LPWORD indices, DWORD idxCount, DWORD) {
    if (!contextAcquired || !verts || !indices || idxCount == 0) return S_OK;
    if ((fvf & D3DFVF_POSITION_MASK) == D3DFVF_XYZRHW) {
        VkRenderer::DrawIndexedPrimitive(type, fvf, verts, vertCount, indices, idxCount);
    } else {
        VkTexHandle* tex = nullptr;
        if (boundTextures[0]) {
            if (boundTextures[0]->IsTextureDirty()) boundTextures[0]->UploadTextureToVk();
            tex = boundTextures[0]->GetVkTexture();
        }
        WorldReconstructor::CaptureDrawCall(type, fvf, verts, vertCount, indices, idxCount,
            (const float*)&worldMatrix, tex);
    }
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

    if ((fvf & D3DFVF_POSITION_MASK) == D3DFVF_XYZRHW) {
        VkRenderer::DrawPrimitive(type, fvf, verts, numVertices);
    } else {
        VkTexHandle* tex = nullptr;
        if (boundTextures[0]) {
            if (boundTextures[0]->IsTextureDirty()) boundTextures[0]->UploadTextureToVk();
            tex = boundTextures[0]->GetVkTexture();
        }
        WorldReconstructor::CaptureDrawCall(type, fvf, verts, numVertices, nullptr, 0,
            (const float*)&worldMatrix, tex);
    }

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

    if ((fvf & D3DFVF_POSITION_MASK) == D3DFVF_XYZRHW) {
        VkRenderer::DrawIndexedPrimitive(type, fvf, verts, numVertices, indices, idxCount);
    } else {
        VkTexHandle* tex = nullptr;
        if (boundTextures[0]) {
            if (boundTextures[0]->IsTextureDirty()) boundTextures[0]->UploadTextureToVk();
            tex = boundTextures[0]->GetVkTexture();
        }
        WorldReconstructor::CaptureDrawCall(type, fvf, verts, numVertices, indices, idxCount,
            (const float*)&worldMatrix, tex);
    }

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
    if (contextAcquired) {
        StubDirectDrawSurface7* surf = boundTextures[stage];
        VkTexHandle* vkTex = nullptr;
        if (surf) {
            if (surf->IsTextureDirty()) surf->UploadTextureToVk();
            vkTex = surf->GetVkTexture();
        }
        if (stage == 0) VkRenderer::BindTexture(vkTex);
        else if (stage == 1) VkRenderer::BindTexture2(vkTex);
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE StubDirect3DDevice7::GetTextureStageState(DWORD, D3DTEXTURESTAGESTATETYPE, LPDWORD v) { if (v) *v = 0; return S_OK; }

HRESULT STDMETHODCALLTYPE StubDirect3DDevice7::SetTextureStageState(DWORD stage, D3DTEXTURESTAGESTATETYPE type, DWORD value) {
    if (!contextAcquired) return S_OK;
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
        case D3DTSS_ALPHAOP:
            VkRenderer::SetStageAlphaOp(stage, value);
            break;
        case D3DTSS_ALPHAARG1:
            VkRenderer::SetStageAlphaArg(stage, 1, value);
            break;
        case D3DTSS_ALPHAARG2:
            VkRenderer::SetStageAlphaArg(stage, 2, value);
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
