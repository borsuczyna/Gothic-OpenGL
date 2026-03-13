#pragma once
// WorldReconstructor.h — Captures drawn triangles and reconstructs world positions
//
// Per draw call, vertices are transformed from their input space to world space:
//   - XYZ vertices:    world_pos = vertex * WorldMatrix
//   - XYZRHW vertices: skipped (2D UI elements)
//
// Triangle topology is preserved for 3D rendering.
// Accumulated triangles are rendered via VkRenderer::DrawReconstructedWorld().

#include <cstdint>
#include <vector>
#include <d3d.h>

struct VkTexHandle; // forward declare

namespace WorldReconstructor {

struct WorldVertex {
    float x, y, z;
    float u, v;
    uint32_t color; // ARGB D3DCOLOR
};

struct DrawBatch {
    VkTexHandle* texture;
    uint32_t startVertex;
    uint32_t vertexCount;
};

// Call at the start of each frame (BeginScene) to clear accumulated data
void BeginFrame();

// Call from every draw call to capture vertices and reconstruct world positions.
// texture is the currently bound VkTexHandle (stage 0), may be nullptr.
void CaptureDrawCall(D3DPRIMITIVETYPE primType,
                     DWORD fvf,
                     const void* vertices, DWORD vertexCount,
                     const WORD* indices, DWORD indexCount,
                     const float* worldMatrix,
                     VkTexHandle* texture);

// Call once per frame to print stats
void PrintIfReady();

// Returns accumulated world vertices in triangle-list order
const std::vector<WorldVertex>& GetWorldVertices();

// Returns draw batches (grouped by texture)
const std::vector<DrawBatch>& GetBatches();

} // namespace WorldReconstructor
