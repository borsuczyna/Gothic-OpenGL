#version 450

layout(push_constant) uniform PushConstants {
    mat4 mvp;
    uint flags;
    float alphaRef;
} pc;

layout(set = 0, binding = 0) uniform sampler2D texSampler;

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragTexCoord;

layout(location = 0) out vec4 outColor;

void main() {
    vec4 color = fragColor;
    if ((pc.flags & 1u) != 0u) {
        color *= texture(texSampler, fragTexCoord);
    }
    if ((pc.flags & 2u) != 0u) {
        if (color.a < pc.alphaRef) discard;
    }
    outColor = color;
}
