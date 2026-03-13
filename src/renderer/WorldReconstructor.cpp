#include "WorldReconstructor.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <windows.h>

namespace WorldReconstructor {

// ---------------------------------------------------------------------------
// Internal state
// ---------------------------------------------------------------------------
static std::vector<WorldVertex> s_worldVerts;
static ULONGLONG s_lastPrintMs = 0;

// Reserve a reasonable amount to avoid per-frame reallocs
static constexpr size_t RESERVE_SIZE = 8192;

// ---------------------------------------------------------------------------
// 4x4 matrix helpers (row-major, matching D3D7 convention)
// ---------------------------------------------------------------------------

// Multiply two row-major 4x4 matrices: out = A * B
static void MatMul4x4(float* out, const float* a, const float* b) {
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            out[r * 4 + c] = a[r * 4 + 0] * b[0 * 4 + c]
                           + a[r * 4 + 1] * b[1 * 4 + c]
                           + a[r * 4 + 2] * b[2 * 4 + c]
                           + a[r * 4 + 3] * b[3 * 4 + c];
        }
    }
}

// Transform a point by a row-major 4x4 matrix: result = point * M (D3D row-vector convention)
static WorldVertex TransformPoint(float px, float py, float pz, const float* m) {
    WorldVertex r;
    r.x = px * m[0] + py * m[4] + pz * m[8]  + m[12];
    r.y = px * m[1] + py * m[5] + pz * m[9]  + m[13];
    r.z = px * m[2] + py * m[6] + pz * m[10] + m[14];
    return r;
}

// Invert a 4x4 matrix (general case). Returns false if singular.
static bool Invert4x4(float* inv, const float* m) {
    float t[16];
    t[0]  =  m[5]*m[10]*m[15] - m[5]*m[11]*m[14] - m[9]*m[6]*m[15] + m[9]*m[7]*m[14] + m[13]*m[6]*m[11] - m[13]*m[7]*m[10];
    t[4]  = -m[4]*m[10]*m[15] + m[4]*m[11]*m[14] + m[8]*m[6]*m[15] - m[8]*m[7]*m[14] - m[12]*m[6]*m[11] + m[12]*m[7]*m[10];
    t[8]  =  m[4]*m[9]*m[15]  - m[4]*m[11]*m[13] - m[8]*m[5]*m[15] + m[8]*m[7]*m[13] + m[12]*m[5]*m[11] - m[12]*m[7]*m[9];
    t[12] = -m[4]*m[9]*m[14]  + m[4]*m[10]*m[13] + m[8]*m[5]*m[14] - m[8]*m[6]*m[13] - m[12]*m[5]*m[10] + m[12]*m[6]*m[9];
    t[1]  = -m[1]*m[10]*m[15] + m[1]*m[11]*m[14] + m[9]*m[2]*m[15] - m[9]*m[3]*m[14] - m[13]*m[2]*m[11] + m[13]*m[3]*m[10];
    t[5]  =  m[0]*m[10]*m[15] - m[0]*m[11]*m[14] - m[8]*m[2]*m[15] + m[8]*m[3]*m[14] + m[12]*m[2]*m[11] - m[12]*m[3]*m[10];
    t[9]  = -m[0]*m[9]*m[15]  + m[0]*m[11]*m[13] + m[8]*m[1]*m[15] - m[8]*m[3]*m[13] - m[12]*m[1]*m[11] + m[12]*m[3]*m[9];
    t[13] =  m[0]*m[9]*m[14]  - m[0]*m[10]*m[13] - m[8]*m[1]*m[14] + m[8]*m[2]*m[13] + m[12]*m[1]*m[10] - m[12]*m[2]*m[9];
    t[2]  =  m[1]*m[6]*m[15]  - m[1]*m[7]*m[14]  - m[5]*m[2]*m[15] + m[5]*m[3]*m[14] + m[13]*m[2]*m[7]  - m[13]*m[3]*m[6];
    t[6]  = -m[0]*m[6]*m[15]  + m[0]*m[7]*m[14]  + m[4]*m[2]*m[15] - m[4]*m[3]*m[14] - m[12]*m[2]*m[7]  + m[12]*m[3]*m[6];
    t[10] =  m[0]*m[5]*m[15]  - m[0]*m[7]*m[13]  - m[4]*m[1]*m[15] + m[4]*m[3]*m[13] + m[12]*m[1]*m[7]  - m[12]*m[3]*m[5];
    t[14] = -m[0]*m[5]*m[14]  + m[0]*m[6]*m[13]  + m[4]*m[1]*m[14] - m[4]*m[2]*m[13] - m[12]*m[1]*m[6]  + m[12]*m[2]*m[5];
    t[3]  = -m[1]*m[6]*m[11]  + m[1]*m[7]*m[10]  + m[5]*m[2]*m[11] - m[5]*m[3]*m[10] - m[9]*m[2]*m[7]   + m[9]*m[3]*m[6];
    t[7]  =  m[0]*m[6]*m[11]  - m[0]*m[7]*m[10]  - m[4]*m[2]*m[11] + m[4]*m[3]*m[10] + m[8]*m[2]*m[7]   - m[8]*m[3]*m[6];
    t[11] = -m[0]*m[5]*m[11]  + m[0]*m[7]*m[9]   + m[4]*m[1]*m[11] - m[4]*m[3]*m[9]  - m[8]*m[1]*m[7]   + m[8]*m[3]*m[5];
    t[15] =  m[0]*m[5]*m[10]  - m[0]*m[6]*m[9]   - m[4]*m[1]*m[10] + m[4]*m[2]*m[9]  + m[8]*m[1]*m[6]   - m[8]*m[2]*m[5];

    float det = m[0]*t[0] + m[1]*t[4] + m[2]*t[8] + m[3]*t[12];
    if (fabsf(det) < 1e-12f) return false;

    float invDet = 1.0f / det;
    for (int i = 0; i < 16; i++)
        inv[i] = t[i] * invDet;
    return true;
}

// ---------------------------------------------------------------------------
// FVF stride calculation (same logic as VkRenderer / ProxyDevice7)
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// Extract position (x, y, z) from a raw FVF vertex
// ---------------------------------------------------------------------------

static void ExtractPosition(const unsigned char* ptr, DWORD fvf,
                             float& outX, float& outY, float& outZ, float& outRHW) {
    outX = *(const float*)(ptr + 0);
    outY = *(const float*)(ptr + 4);
    outZ = *(const float*)(ptr + 8);
    outRHW = 1.0f;
    if ((fvf & D3DFVF_POSITION_MASK) == D3DFVF_XYZRHW)
        outRHW = *(const float*)(ptr + 12);
}

// ---------------------------------------------------------------------------
// Unproject an XYZRHW screen-space vertex back to world space
// ---------------------------------------------------------------------------

static WorldVertex UnprojectRHW(float sx, float sy, float sz, float rhw,
                                 const float* invViewProj,
                                 const D3DVIEWPORT7& vp) {
    // Screen → NDC
    float ndcX = (2.0f * (sx - vp.dwX) / (float)vp.dwWidth) - 1.0f;
    float ndcY = 1.0f - (2.0f * (sy - vp.dwY) / (float)vp.dwHeight); // D3D Y is top-down
    float ndcZ = sz; // D3D depth is already 0..1

    // NDC → clip (w = 1/rhw for pre-divided vertices)
    float w = (rhw > 1e-6f) ? (1.0f / rhw) : 1.0f;
    float clipX = ndcX * w;
    float clipY = ndcY * w;
    float clipZ = ndcZ * w;
    float clipW = w;

    // clip → world via inverse(View*Proj), row-vector convention: [x y z w] * M^-1
    WorldVertex r;
    r.x = clipX * invViewProj[0] + clipY * invViewProj[4] + clipZ * invViewProj[8]  + clipW * invViewProj[12];
    r.y = clipX * invViewProj[1] + clipY * invViewProj[5] + clipZ * invViewProj[9]  + clipW * invViewProj[13];
    r.z = clipX * invViewProj[2] + clipY * invViewProj[6] + clipZ * invViewProj[10] + clipW * invViewProj[14];
    float rw = clipX * invViewProj[3] + clipY * invViewProj[7] + clipZ * invViewProj[11] + clipW * invViewProj[15];
    if (fabsf(rw) > 1e-6f) {
        r.x /= rw;
        r.y /= rw;
        r.z /= rw;
    }
    return r;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void BeginFrame() {
    s_worldVerts.clear();
    if (s_worldVerts.capacity() < RESERVE_SIZE)
        s_worldVerts.reserve(RESERVE_SIZE);
}

void CaptureDrawCall(DWORD fvf,
                     const void* vertices, DWORD vertexCount,
                     const WORD* indices, DWORD indexCount,
                     const float* worldMatrix,
                     const float* viewMatrix,
                     const float* projMatrix,
                     const D3DVIEWPORT7& viewport) {
    if (!vertices || vertexCount == 0) return;

    bool isRHW = (fvf & D3DFVF_POSITION_MASK) == D3DFVF_XYZRHW;
    DWORD stride = CalcFVFStride(fvf);
    const unsigned char* src = (const unsigned char*)vertices;

    // Pre-compute inverse(View * Proj) for RHW unprojection
    float invVP[16];
    bool haveInvVP = false;
    if (isRHW) {
        float vp[16];
        MatMul4x4(vp, viewMatrix, projMatrix);
        haveInvVP = Invert4x4(invVP, vp);
    }

    // Determine which vertices to process
    // For indexed draws, iterate unique indices; for non-indexed, iterate all
    if (indices && indexCount > 0) {
        for (DWORD i = 0; i < indexCount; i++) {
            WORD idx = indices[i];
            if (idx >= vertexCount) continue;
            const unsigned char* ptr = src + idx * stride;

            float px, py, pz, rhw;
            ExtractPosition(ptr, fvf, px, py, pz, rhw);

            WorldVertex wv;
            if (isRHW && haveInvVP) {
                wv = UnprojectRHW(px, py, pz, rhw, invVP, viewport);
            } else {
                wv = TransformPoint(px, py, pz, worldMatrix);
            }
            s_worldVerts.push_back(wv);
        }
    } else {
        for (DWORD i = 0; i < vertexCount; i++) {
            const unsigned char* ptr = src + i * stride;

            float px, py, pz, rhw;
            ExtractPosition(ptr, fvf, px, py, pz, rhw);

            WorldVertex wv;
            if (isRHW && haveInvVP) {
                wv = UnprojectRHW(px, py, pz, rhw, invVP, viewport);
            } else {
                wv = TransformPoint(px, py, pz, worldMatrix);
            }
            s_worldVerts.push_back(wv);
        }
    }
}

void PrintIfReady() {
    ULONGLONG now = GetTickCount64();
    if (now - s_lastPrintMs < 1000) return;
    s_lastPrintMs = now;

    if (s_worldVerts.empty()) {
        printf("[WorldReconstructor] No vertices this frame\n");
        fflush(stdout);
        return;
    }

    printf("[WorldReconstructor] %zu world vertices:\n", s_worldVerts.size());
    // Print a summary: first 20 and last 5 to keep output manageable
    size_t total = s_worldVerts.size();
    size_t printHead = (total > 20) ? 20 : total;
    for (size_t i = 0; i < printHead; i++) {
        const auto& v = s_worldVerts[i];
        // printf("  [%zu] (%.1f, %.1f, %.1f)\n", i, v.x, v.y, v.z);
    }
    if (total > 25) {
        // printf("  ... (%zu vertices omitted) ...\n", total - 25);
        for (size_t i = total - 5; i < total; i++) {
            const auto& v = s_worldVerts[i];
            // printf("  [%zu] (%.1f, %.1f, %.1f)\n", i, v.x, v.y, v.z);
        }
    } else if (total > printHead) {
        for (size_t i = printHead; i < total; i++) {
            const auto& v = s_worldVerts[i];
            // printf("  [%zu] (%.1f, %.1f, %.1f)\n", i, v.x, v.y, v.z);
        }
    }
    fflush(stdout);
}

const std::vector<WorldVertex>& GetWorldVertices() {
    return s_worldVerts;
}

} // namespace WorldReconstructor
