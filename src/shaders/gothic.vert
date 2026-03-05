#version 450

layout(push_constant) uniform PushConstants {
    mat4 mvp;
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
} pc;

layout(location = 0) in vec4 inPosition;
layout(location = 1) in uint inColor;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec2 inTexCoord2;

layout(location = 0) out vec4 fragColor;
layout(location = 1) out vec2 fragTexCoord;
layout(location = 2) out vec2 fragTexCoord2;

void main() {
    // inPosition.w = 1.0 for 3D geometry (standard transform)
    // inPosition.w = reconstructed W for XYZRHW (pre-transformed, MVP is identity)
    gl_Position = pc.mvp * inPosition;
    fragColor = vec4(
        float((inColor >> 16u) & 0xFFu) / 255.0,
        float((inColor >>  8u) & 0xFFu) / 255.0,
        float( inColor         & 0xFFu) / 255.0,
        float((inColor >> 24u) & 0xFFu) / 255.0
    );
    fragTexCoord = inTexCoord;
    fragTexCoord2 = inTexCoord2;
}
