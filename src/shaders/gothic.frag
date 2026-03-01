#version 450

layout(push_constant) uniform PushConstants {
    mat4 mvp;
    uint flags;
    float alphaRef;
    uint stage0ColorOp;
    uint stage1ColorOp;
    uint stage0Args;   // low 16 = colorArg1, high 16 = colorArg2
    uint stage1Args;
    uint texCoordIdx;  // low 16 = stage0 UV index, high 16 = stage1 UV index
} pc;

layout(set = 0, binding = 0) uniform sampler2D texSampler;
layout(set = 1, binding = 0) uniform sampler2D texSampler2;

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in vec2 fragTexCoord2;

layout(location = 0) out vec4 outColor;

vec2 getUV(uint idx) {
    return (idx == 0u) ? fragTexCoord : fragTexCoord2;
}

vec4 selectArg(uint arg, vec4 current, vec4 diffuse, vec4 texColor) {
    if (arg == 0u) return diffuse;
    if (arg == 1u) return current;
    if (arg == 2u) return texColor;
    return diffuse;
}

vec4 runStage(uint op, uint args, vec4 current, vec4 diffuse, vec4 texColor) {
    uint a1id = args & 0xFFFFu;
    uint a2id = args >> 16u;
    vec4 a1 = selectArg(a1id, current, diffuse, texColor);
    vec4 a2 = selectArg(a2id, current, diffuse, texColor);

    if (op == 2u)  return a1;                                      // SELECTARG1
    if (op == 3u)  return a2;                                      // SELECTARG2
    if (op == 4u)  return a1 * a2;                                 // MODULATE
    if (op == 5u)  return clamp(a1 * a2 * 2.0, 0.0, 1.0);        // MODULATE2X
    if (op == 6u)  return clamp(a1 * a2 * 4.0, 0.0, 1.0);        // MODULATE4X
    if (op == 7u)  return clamp(a1 + a2, 0.0, 1.0);              // ADD
    if (op == 10u) return clamp(a1 - a2, 0.0, 1.0);              // SUBTRACT
    if (op == 11u) return clamp(a1 + a2 - a1 * a2, 0.0, 1.0);   // ADDSMOOTH
    if (op == 12u) return a1 * diffuse.a + a2 * (1.0 - diffuse.a); // BLENDDIFFUSEALPHA
    if (op == 13u) return a1 * texColor.a + a2 * (1.0 - texColor.a); // BLENDTEXTUREALPHA
    return a1 * a2;
}

void main() {
    vec4 diffuse = fragColor;

    uint tc0 = pc.texCoordIdx & 0xFFFFu;
    uint tc1 = pc.texCoordIdx >> 16u;

    bool hasTex0 = (pc.flags & 1u) != 0u;
    vec4 tex0 = hasTex0 ? texture(texSampler, getUV(tc0)) : vec4(1.0);

    vec4 color;
    uint op0 = pc.stage0ColorOp;
    if (op0 <= 1u)
        color = diffuse;
    else
        color = runStage(op0, pc.stage0Args, diffuse, diffuse, tex0);

    bool hasTex1 = (pc.flags & 4u) != 0u;
    uint op1 = pc.stage1ColorOp;
    if (hasTex1 && op1 > 1u) {
        vec4 tex1 = texture(texSampler2, getUV(tc1));
        float prevAlpha = color.a;
        color = runStage(op1, pc.stage1Args, color, diffuse, tex1);
        color.a = prevAlpha;
    }

    if ((pc.flags & 2u) != 0u) {
        if (color.a < pc.alphaRef) discard;
    }
    outColor = color;
}
