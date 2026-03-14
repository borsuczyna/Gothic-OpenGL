#version 450

layout(push_constant) uniform PushConstants {
    mat4 world;
    uint flags;
    float alphaRef;
    uint stage0ColorOp;
    uint stage1ColorOp;
    uint stage0Args;
    uint stage1Args;
    uint texCoordIdx;
    uint stage0AlphaOp;
    uint stage0AlphaArgs;
    uint textureFactor;
    uint timecycleColor;
    // Individual floats to avoid vec2 alignment padding (std430 aligns vec2 to 8 bytes)
    float vpPosX;
    float vpPosY;
    float vpSizeW;
    float vpSizeH;
} pc;

// View and Projection matrices in a per-frame UBO — all matrix math happens here in the shader
layout(std140, set = 2, binding = 0) uniform ViewProjUBO {
    mat4 view;
    mat4 proj;
} vp;

// Matching GD3D11's ExVertexStruct / VS_INPUT layout
layout(location = 0) in vec3 inPosition;    // Position
layout(location = 1) in vec3 inNormal;      // Normal  (inNormal.x = RHW for pre-transformed)
layout(location = 2) in vec2 inTexCoord;    // TexCoord set 0
layout(location = 3) in vec2 inTexCoord2;   // TexCoord set 1
layout(location = 4) in uint inColor;       // Packed ARGB (D3DCOLOR)

layout(location = 0) out vec4 fragColor;
layout(location = 1) out vec2 fragTexCoord;
layout(location = 2) out vec2 fragTexCoord2;

void main() {
    bool isRHW = (pc.flags & 32u) != 0u;

    if (isRHW) {
        // TransformXYZRHW — matches GD3D11's VS_TransformedEx.hlsl
        // RHW is stored in inNormal.x (same as GD3D11 stores it in ExVertexStruct.Normal.x)
        float rhw = inNormal.x;

        // Convert viewport pixel coordinates to NDC
        // Vulkan NDC: X [-1,+1] left-to-right, Y [-1,+1] top-to-bottom
        vec2 ndc_xy;
        ndc_xy.x = ((2.0 * (inPosition.x - pc.vpPosX)) / pc.vpSizeW) - 1.0;
        ndc_xy.y = ((2.0 * (inPosition.y - pc.vpPosY)) / pc.vpSizeH) - 1.0;
        float ndc_z = inPosition.z;

        // Reconstruct actual W from RHW and convert NDC to clip space
        float actualW = 1.0 / rhw;
        gl_Position = vec4(ndc_xy * actualW, ndc_z * actualW, actualW);
    } else {
        // Full 3D transform: World * View * Projection — all done here in the shader
        // D3D row-major matrices memcpy'd into GLSL column-major mat4 = implicit transpose,
        // so GLSL's (M * v) equals D3D's (v * M).  Multiply order: proj * view * world * pos
        vec4 worldPos = pc.world * vec4(inPosition, 1.0);
        vec4 viewPos  = vp.view  * worldPos;
        gl_Position   = vp.proj  * viewPos;

        // Flip Y for Vulkan NDC (Vulkan Y is inverted vs D3D clip space)
        gl_Position.y = -gl_Position.y;
    }

    // Unpack D3DCOLOR (ARGB packed DWORD) to vec4 RGBA
    fragColor = vec4(
        float((inColor >> 16u) & 0xFFu) / 255.0,
        float((inColor >>  8u) & 0xFFu) / 255.0,
        float( inColor         & 0xFFu) / 255.0,
        float((inColor >> 24u) & 0xFFu) / 255.0
    );
    fragTexCoord = inTexCoord;
    fragTexCoord2 = inTexCoord2;
}
