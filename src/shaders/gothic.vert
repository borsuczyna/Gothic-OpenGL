#version 450

layout(push_constant) uniform PushConstants {
    mat4 mvp;
    uint flags;
    float alphaRef;
} pc;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in uint inColor;
layout(location = 2) in vec2 inTexCoord;

layout(location = 0) out vec4 fragColor;
layout(location = 1) out vec2 fragTexCoord;

void main() {
    gl_Position = pc.mvp * vec4(inPosition, 1.0);
    fragColor = vec4(
        float((inColor >> 16u) & 0xFFu) / 255.0,
        float((inColor >>  8u) & 0xFFu) / 255.0,
        float( inColor         & 0xFFu) / 255.0,
        float((inColor >> 24u) & 0xFFu) / 255.0
    );
    fragTexCoord = inTexCoord;
}
