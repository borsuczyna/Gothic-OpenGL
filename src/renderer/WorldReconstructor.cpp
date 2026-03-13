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
static std::vector<DrawBatch>   s_batches;
static ULONGLONG s_lastPrintMs = 0;
static constexpr size_t RESERVE_SIZE = 65536;

// ---------------------------------------------------------------------------
// Transform a point by a row-major 4x4 matrix (D3D row-vector convention)
// ---------------------------------------------------------------------------
static void TransformPoint(float px, float py, float pz, const float* m,
                           float& ox, float& oy, float& oz) {
    ox = px * m[0] + py * m[4] + pz * m[8]  + m[12];
    oy = px * m[1] + py * m[5] + pz * m[9]  + m[13];
    oz = px * m[2] + py * m[6] + pz * m[10] + m[14];
}

// ---------------------------------------------------------------------------
// FVF helpers
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

// Compute byte offset to diffuse color within an FVF vertex
static DWORD CalcColorOffset(DWORD fvf) {
    DWORD offset = 0;
    switch (fvf & D3DFVF_POSITION_MASK) {
        case D3DFVF_XYZ:    offset = 12; break;
        case D3DFVF_XYZRHW: offset = 16; break;
        case D3DFVF_XYZB1:  offset = 16; break;
        case D3DFVF_XYZB2:  offset = 20; break;
        case D3DFVF_XYZB3:  offset = 24; break;
        case D3DFVF_XYZB4:  offset = 28; break;
        case D3DFVF_XYZB5:  offset = 32; break;
    }
    if (fvf & D3DFVF_NORMAL) offset += 12;
    return offset;
}

// Compute byte offset to first UV set within an FVF vertex
static DWORD CalcUVOffset(DWORD fvf) {
    DWORD offset = CalcColorOffset(fvf);
    if (fvf & D3DFVF_DIFFUSE)  offset += 4;
    if (fvf & D3DFVF_SPECULAR) offset += 4;
    return offset;
}

// Extract world-space vertex with UV, UV2, and color from raw FVF data
static WorldVertex ExtractWorldVertex(const unsigned char* ptr, DWORD fvf,
                                      const float* worldMatrix) {
    WorldVertex wv;
    float px = *(const float*)(ptr + 0);
    float py = *(const float*)(ptr + 4);
    float pz = *(const float*)(ptr + 8);
    TransformPoint(px, py, pz, worldMatrix, wv.x, wv.y, wv.z);

    if (fvf & D3DFVF_DIFFUSE) {
        wv.color = *(const uint32_t*)(ptr + CalcColorOffset(fvf));
    } else {
        wv.color = 0xFFFFFFFF;
    }

    DWORD texCount = (fvf & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT;
    DWORD uvOff = CalcUVOffset(fvf);
    if (texCount > 0) {
        wv.u = *(const float*)(ptr + uvOff);
        wv.v = *(const float*)(ptr + uvOff + 4);
    } else {
        wv.u = 0.0f;
        wv.v = 0.0f;
    }
    if (texCount > 1) {
        // Second tex coord set (lightmaps/detail maps)
        // First set is 2 floats (8 bytes) for standard 2D coords
        DWORD uv2Off = uvOff + 8;
        wv.u2 = *(const float*)(ptr + uv2Off);
        wv.v2 = *(const float*)(ptr + uv2Off + 4);
    } else {
        wv.u2 = 0.0f;
        wv.v2 = 0.0f;
    }
    return wv;
}

// Emit one triangle into the accumulator
static inline void EmitTriangle(const WorldVertex& a, const WorldVertex& b, const WorldVertex& c) {
    s_worldVerts.push_back(a);
    s_worldVerts.push_back(b);
    s_worldVerts.push_back(c);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void BeginFrame() {
    s_worldVerts.clear();
    s_batches.clear();
    if (s_worldVerts.capacity() < RESERVE_SIZE)
        s_worldVerts.reserve(RESERVE_SIZE);
}

void CaptureDrawCall(D3DPRIMITIVETYPE primType,
                     DWORD fvf,
                     const void* vertices, DWORD vertexCount,
                     const WORD* indices, DWORD indexCount,
                     const float* worldMatrix,
                     VkTexHandle* texture,
                     const BatchRenderState& rs) {
    if (!vertices || vertexCount == 0) return;

    // Skip pre-transformed 2D vertices (UI, HUD)
    if ((fvf & D3DFVF_POSITION_MASK) == D3DFVF_XYZRHW) return;

    DWORD stride = CalcFVFStride(fvf);
    const unsigned char* src = (const unsigned char*)vertices;
    uint32_t startVert = (uint32_t)s_worldVerts.size();

    auto getVert = [&](DWORD i) -> WorldVertex {
        return ExtractWorldVertex(src + i * stride, fvf, worldMatrix);
    };

    if (indices && indexCount > 0) {
        switch (primType) {
        case D3DPT_TRIANGLELIST:
            for (DWORD i = 0; i + 2 < indexCount; i += 3) {
                if (indices[i] >= vertexCount || indices[i+1] >= vertexCount || indices[i+2] >= vertexCount) continue;
                EmitTriangle(getVert(indices[i]), getVert(indices[i+1]), getVert(indices[i+2]));
            }
            break;
        case D3DPT_TRIANGLEFAN:
            if (indexCount >= 3 && indices[0] < vertexCount) {
                WorldVertex v0 = getVert(indices[0]);
                for (DWORD i = 1; i + 1 < indexCount; i++) {
                    if (indices[i] >= vertexCount || indices[i+1] >= vertexCount) continue;
                    EmitTriangle(v0, getVert(indices[i]), getVert(indices[i+1]));
                }
            }
            break;
        case D3DPT_TRIANGLESTRIP:
            for (DWORD i = 0; i + 2 < indexCount; i++) {
                if (indices[i] >= vertexCount || indices[i+1] >= vertexCount || indices[i+2] >= vertexCount) continue;
                if (i & 1)
                    EmitTriangle(getVert(indices[i]), getVert(indices[i+2]), getVert(indices[i+1]));
                else
                    EmitTriangle(getVert(indices[i]), getVert(indices[i+1]), getVert(indices[i+2]));
            }
            break;
        default:
            break;
        }
    } else {
        switch (primType) {
        case D3DPT_TRIANGLELIST:
            for (DWORD i = 0; i + 2 < vertexCount; i += 3)
                EmitTriangle(getVert(i), getVert(i+1), getVert(i+2));
            break;
        case D3DPT_TRIANGLEFAN:
            if (vertexCount >= 3) {
                WorldVertex v0 = getVert(0);
                for (DWORD i = 1; i + 1 < vertexCount; i++)
                    EmitTriangle(v0, getVert(i), getVert(i+1));
            }
            break;
        case D3DPT_TRIANGLESTRIP:
            for (DWORD i = 0; i + 2 < vertexCount; i++) {
                if (i & 1)
                    EmitTriangle(getVert(i), getVert(i+2), getVert(i+1));
                else
                    EmitTriangle(getVert(i), getVert(i+1), getVert(i+2));
            }
            break;
        default:
            break;
        }
    }

    uint32_t emitted = (uint32_t)s_worldVerts.size() - startVert;
    if (emitted > 0) {
        // Merge with previous batch if same texture and same render state
        bool canMerge = false;
        if (!s_batches.empty()) {
            const auto& prev = s_batches.back();
            canMerge = prev.texture == texture &&
                prev.rs.blendEnabled == rs.blendEnabled &&
                prev.rs.srcBlend == rs.srcBlend &&
                prev.rs.dstBlend == rs.dstBlend &&
                prev.rs.alphaTestEnabled == rs.alphaTestEnabled &&
                prev.rs.depthWriteEnabled == rs.depthWriteEnabled &&
                prev.rs.stage0ColorOp == rs.stage0ColorOp &&
                prev.rs.stage1ColorOp == rs.stage1ColorOp &&
                prev.rs.stage0Arg1 == rs.stage0Arg1 &&
                prev.rs.stage0Arg2 == rs.stage0Arg2 &&
                prev.rs.stage1Arg1 == rs.stage1Arg1 &&
                prev.rs.stage1Arg2 == rs.stage1Arg2 &&
                prev.rs.stage0AlphaOp == rs.stage0AlphaOp &&
                prev.rs.stage0AlphaArg1 == rs.stage0AlphaArg1 &&
                prev.rs.stage0AlphaArg2 == rs.stage0AlphaArg2 &&
                prev.rs.stage0TexCoordIdx == rs.stage0TexCoordIdx &&
                prev.rs.stage1TexCoordIdx == rs.stage1TexCoordIdx &&
                prev.rs.textureFactor == rs.textureFactor &&
                prev.rs.texture2 == rs.texture2;
        }
        if (canMerge) {
            s_batches.back().vertexCount += emitted;
        } else {
            DrawBatch b;
            b.texture = texture;
            b.startVertex = startVert;
            b.vertexCount = emitted;
            b.rs = rs;
            s_batches.push_back(b);
        }
    }
}

void PrintIfReady() {
    ULONGLONG now = GetTickCount64();
    if (now - s_lastPrintMs < 1000) return;
    s_lastPrintMs = now;

    size_t triCount = s_worldVerts.size() / 3;
    printf("[WorldReconstructor] %zu triangles, %zu batches\n", triCount, s_batches.size());
    fflush(stdout);
}

const std::vector<WorldVertex>& GetWorldVertices() {
    return s_worldVerts;
}

const std::vector<DrawBatch>& GetBatches() {
    return s_batches;
}

} // namespace WorldReconstructor
