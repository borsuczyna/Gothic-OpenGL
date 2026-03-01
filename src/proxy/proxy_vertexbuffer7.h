#pragma once

#include <d3d.h>
#include <vector>

class StubDirect3DVertexBuffer7 : public IDirect3DVertexBuffer7 {
    int refCount = 1;
    D3DVERTEXBUFFERDESC vbDesc = {};
    std::vector<unsigned char> data;

public:
    explicit StubDirect3DVertexBuffer7(const D3DVERTEXBUFFERDESC& d) : vbDesc(d) {
        data.resize(d.dwNumVertices * 64, 0);
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void** ppv) override { *ppv = this; AddRef(); return S_OK; }
    ULONG   STDMETHODCALLTYPE AddRef() override { return ++refCount; }
    ULONG   STDMETHODCALLTYPE Release() override { if (--refCount == 0) { delete this; return 0; } return refCount; }

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
