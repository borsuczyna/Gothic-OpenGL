#pragma once

#include <cstdint>
#include <cstring>
#include <functional>

namespace gvlk {

struct GVertex {
    float    x = 0, y = 0, z = 0;
    uint32_t color = 0;
    float    u = 0, v = 0;
    float    u2 = 0, v2 = 0;
    float    wx = 0, wy = 0, wz = 0;
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

struct ShadowConfig {
    uint32_t resolution       = 2048;
    int      cascadeCount     = 2;
    float    shadowRange      = 15000.0f;
    float    depthBiasConstant = 4.0f;
    float    depthBiasSlope    = 1.5f;
    float    shadowStrength   = 0.5f;
};

struct ShadowUBO {
    float cascadeVP[2][16];
    float cascadeSplits[4];
    float shadowStrength;
    float shadowEnabled;
    float pad[2];
};

} // namespace gvlk
