#pragma once

#include <ddraw.h>

class StubDirectDrawClipper : public IDirectDrawClipper {
    int refCount = 1;
    HWND clipperHwnd = nullptr;

public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void** ppv) override { *ppv = this; AddRef(); return S_OK; }
    ULONG   STDMETHODCALLTYPE AddRef() override { return ++refCount; }
    ULONG   STDMETHODCALLTYPE Release() override { if (--refCount == 0) { delete this; return 0; } return refCount; }

    HRESULT STDMETHODCALLTYPE GetClipList(LPRECT, LPRGNDATA, LPDWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetHWnd(HWND* h) override { if (h) *h = clipperHwnd; return S_OK; }
    HRESULT STDMETHODCALLTYPE Initialize(LPDIRECTDRAW, DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE IsClipListChanged(BOOL* b) override { if (b) *b = FALSE; return S_OK; }
    HRESULT STDMETHODCALLTYPE SetClipList(LPRGNDATA, DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE SetHWnd(DWORD, HWND h) override { clipperHwnd = h; return S_OK; }
};
