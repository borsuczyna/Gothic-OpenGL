#pragma once

#include <cstdint>
#include <cstring>
#include <functional>

namespace gvlk {

/* ── Vertex structs matching GD3D11/VertexTypes.h ─────────────────────── */

/** We pack most of Gothic's FVF-formats into this vertex-struct (same as GD3D11's ExVertexStruct) */
struct GVertex {
    float    px = 0, py = 0, pz = 0;   // Position  (float3)
    float    nx = 0, ny = 0, nz = 0;   // Normal    (float3)  — nx stores RHW for XYZRHW verts
    float    u  = 0, v  = 0;           // TexCoord  (float2)
    float    u2 = 0, v2 = 0;           // TexCoord2 (float2)
    uint32_t color = 0xFFFFFFFF;        // D3DCOLOR  (ARGB packed DWORD)
};

/* ── Gothic FVF typed vertex structs (matching GD3D11) ────────────────── */

struct Gothic_XYZ_DIF_T1_Vertex {
    float    x, y, z;
    uint32_t color;
    float    u, v;
};

struct Gothic_XYZ_NRM_T1_Vertex {
    float x, y, z;
    float nx, ny, nz;
    float u, v;
};

struct Gothic_XYZ_DIF_T2_Vertex {
    float    x, y, z;
    uint32_t color;
    float    u, v;
    float    u2, v2;
};

struct Gothic_XYZ_NRM_DIF_T2_Vertex {
    float    x, y, z;
    float    nx, ny, nz;
    uint32_t color;
    float    u, v;
    float    u2, v2;
};

struct Gothic_XYZRHW_DIF_T1_Vertex {
    float    x, y, z;
    float    rhw;
    uint32_t color;
    float    u, v;
};

struct Gothic_XYZRHW_DIF_SPEC_T1_Vertex {
    float    x, y, z;
    float    rhw;
    uint32_t color;
    uint32_t specular;
    float    u, v;
};

/* ── Push constants ───────────────────────────────────────────────────── */

struct PushConstants {
    float    world[16] = {};            // World (model-to-world) matrix — shader does the full W*V*P transform
    uint32_t flags = 0;              // bit0=hasTex0, bit1=alphaTest, bit2=hasTex1,
                                     // bit3=texHasAlpha, bit4=timecycle, bit5=isRHW
    float    alphaRef = 0;
    uint32_t stage0ColorOp = 0;
    uint32_t stage1ColorOp = 0;
    uint32_t stage0Args = 0;
    uint32_t stage1Args = 0;
    uint32_t texCoordIdx = 0;
    uint32_t stage0AlphaOp = 2;
    uint32_t stage0AlphaArgs = (1u << 16) | 2u;
    uint32_t textureFactor = 0xFFFFFFFF;
    uint32_t timecycleColor = 0xFFFFFFFF;
    float    vpPos[2] = {};          // Viewport position (for TransformXYZRHW)
    float    vpSize[2] = {};         // Viewport size     (for TransformXYZRHW)
};
// Total: 64 + 11*4 + 2*4 + 2*4 = 124 bytes  (within 128-byte push constant limit)

struct PipelineKey {
    uint8_t blendEnabled = 0;
    uint8_t depthTestEnabled = 0;
    uint8_t depthWriteEnabled = 0;
    uint8_t depthFunc = 0;
    uint8_t srcBlend = 0;
    uint8_t dstBlend = 0;
    uint8_t pad0 = 0, pad1 = 0;

    bool operator==(const PipelineKey& o) const {
        return memcmp(this, &o, sizeof(*this)) == 0;
    }
};

struct PipelineKeyHash {
    size_t operator()(const PipelineKey& k) const {
        uint64_t v;
        memcpy(&v, &k, sizeof(v));
        return std::hash<uint64_t>()(v);
    }
};

} // namespace gvlk
