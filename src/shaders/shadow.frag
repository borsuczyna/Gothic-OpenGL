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

layout(set = 0, binding = 0) uniform sampler2D texSampler;

layout(location = 1) in vec2 fragTexCoord;

void main() {
    if ((pc.flags & 3u) == 3u) {
        float alpha = texture(texSampler, fragTexCoord).a;
        if (alpha < pc.alphaRef) discard;
    }
}
