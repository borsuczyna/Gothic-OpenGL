#pragma once
// WorldReconstructor.h — Captures drawn vertices and reconstructs world positions
//
// Per draw call, vertices are transformed from their input space to world space:
//   - XYZ vertices:    world_pos = vertex * WorldMatrix
//   - XYZRHW vertices: unprojected via inverse(View * Projection) + viewport
//
// Accumulated world positions are printed once per second, then cleared.

#include <cstdint>
#include <vector>
#include <d3d.h>

namespace WorldReconstructor {

struct WorldVertex {
    float x, y, z;
};

// Call at the start of each frame (BeginScene) to clear accumulated data
void BeginFrame();

// Call from every draw call to capture vertices and reconstruct world positions.
// worldMatrix/viewMatrix/projMatrix are the current D3D7 transform state (row-major 4x4).
// viewport is the current D3D7 viewport.
// fvf is the FVF flags, vertices is raw vertex data, count is vertex count.
// indices/indexCount are for indexed draws (nullptr/0 for non-indexed).
void CaptureDrawCall(DWORD fvf,
                     const void* vertices, DWORD vertexCount,
                     const WORD* indices, DWORD indexCount,
                     const float* worldMatrix,
                     const float* viewMatrix,
                     const float* projMatrix,
                     const D3DVIEWPORT7& viewport);

// Call once per frame (or on a timer) to print accumulated world positions
void PrintIfReady();

// Returns accumulated world vertices (read-only) for other consumers
const std::vector<WorldVertex>& GetWorldVertices();

} // namespace WorldReconstructor
