#pragma once

#include <cstdint>
#include <cstring>
#include <functional>

namespace gvlk {

struct GVertex {
    float    x = 0, y = 0, z = 0;
    float    w = 1.0f;   // 1.0 for 3D geometry, reconstructed W for XYZRHW
    uint32_t color = 0;
    float    u = 0, v = 0;
    float    u2 = 0, v2 = 0;
};

struct PushConstants {
    float    mvp[16] = {};
    uint32_t flags = 0;
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
};

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
