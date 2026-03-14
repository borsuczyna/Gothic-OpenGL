#pragma once
// WorldReconstructor.h — Captures drawn triangles and reconstructs world positions
//
// Per draw call, each vertex's position is immediately transformed from model
// space to absolute world space on the CPU using the current D3D world matrix
// (D3D convention: v' = v * M, row-major).  All accumulated vertices are
// therefore in consistent world space.
//
// The shader only needs to apply View and Projection; the world matrix push
// constant is always identity for reconstructed world draws.
//
// Pre-transformed (XYZRHW) vertices are skipped as they belong to the 2D UI.
// Accumulated triangles are rendered via VkRenderer::DrawReconstructedWorld().

#include <cstdint>
#include <vector>
#include <d3d.h>

struct VkTexHandle; // forward declare

namespace WorldReconstructor {

struct WorldVertex {
    float x, y, z;
    float u, v;
    float u2, v2;
    uint32_t color; // ARGB D3DCOLOR
};

// Snapshot of all rendering state needed to replay a batch
struct BatchRenderState {
    // Pipeline state
    bool     blendEnabled;
    uint8_t  srcBlend;
    uint8_t  dstBlend;
    bool     alphaTestEnabled;
    float    alphaRef;
    bool     depthWriteEnabled;
    // Texture stage state
    uint32_t stage0ColorOp;
    uint32_t stage1ColorOp;
    uint32_t stage0Arg1, stage0Arg2;
    uint32_t stage1Arg1, stage1Arg2;
    uint32_t stage0AlphaOp;
    uint32_t stage0AlphaArg1, stage0AlphaArg2;
    uint32_t stage0TexCoordIdx, stage1TexCoordIdx;
    uint32_t textureFactor;
    // Second texture
    VkTexHandle* texture2;
};

struct DrawBatch {
    VkTexHandle* texture;
    uint32_t startVertex;
    uint32_t vertexCount;
    BatchRenderState rs;
    // Vertices are pre-transformed to world space at capture time; no per-batch
    // world matrix is needed.  The GPU applies View and Projection only.
};

// Call at the start of each frame (BeginScene) to clear accumulated data
void BeginFrame();

// Call from every draw call to capture vertices and reconstruct world positions.
// texture is the currently bound VkTexHandle (stage 0), may be nullptr.
// rs is a snapshot of all relevant render state.
void CaptureDrawCall(D3DPRIMITIVETYPE primType,
                     DWORD fvf,
                     const void* vertices, DWORD vertexCount,
                     const WORD* indices, DWORD indexCount,
                     const float* worldMatrix,
                     VkTexHandle* texture,
                     const BatchRenderState& rs);

// Call once per frame to print stats
void PrintIfReady();

// Returns accumulated world vertices in triangle-list order
const std::vector<WorldVertex>& GetWorldVertices();

// Returns draw batches (grouped by compatible state)
const std::vector<DrawBatch>& GetBatches();

} // namespace WorldReconstructor
