#version 450

layout(push_constant) uniform PushConstants {
    mat4 world;
    uint flags;
    float alphaRef;
    uint stage0ColorOp;
    uint stage1ColorOp;
    uint stage0Args;   // low 16 = colorArg1, high 16 = colorArg2
    uint stage1Args;
    uint texCoordIdx;  // low 16 = stage0 UV index, high 16 = stage1 UV index
    uint stage0AlphaOp;
    uint stage0AlphaArgs; // low 16 = alphaArg1, high 16 = alphaArg2
    uint textureFactor;   // packed ARGB
    uint timecycleColor;  // packed ARGB, replaces diffuse for world geometry
    // Individual floats to match C++ layout (avoid vec2 alignment padding)
    float vpPosX;
    float vpPosY;
    float vpSizeW;
    float vpSizeH;
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

vec4 selectArg(uint arg, vec4 current, vec4 diffuse, vec4 texColor, vec4 tfactor) {
    if (arg == 0u) return diffuse;      // D3DTA_DIFFUSE
    if (arg == 1u) return current;      // D3DTA_CURRENT
    if (arg == 2u) return texColor;     // D3DTA_TEXTURE
    if (arg == 3u) return tfactor;      // D3DTA_TFACTOR
    return diffuse;
}

vec4 runStage(uint op, uint args, vec4 current, vec4 diffuse, vec4 texColor, vec4 tfactor) {
    uint a1id = args & 0xFFFFu;
    uint a2id = args >> 16u;
    vec4 a1 = selectArg(a1id, current, diffuse, texColor, tfactor);
    vec4 a2 = selectArg(a2id, current, diffuse, texColor, tfactor);

    if (op == 2u)  return a1;                                                    // SELECTARG1
    if (op == 3u)  return a2;                                                    // SELECTARG2
    if (op == 4u)  return a1 * a2;                                               // MODULATE
    if (op == 5u)  return clamp(a1 * a2 * 2.0, 0.0, 1.0);                      // MODULATE2X
    if (op == 6u)  return clamp(a1 * a2 * 4.0, 0.0, 1.0);                      // MODULATE4X
    if (op == 7u)  return clamp(a1 + a2, 0.0, 1.0);                            // ADD
    if (op == 10u) return clamp(a1 - a2, 0.0, 1.0);                            // SUBTRACT
    if (op == 11u) return clamp(a1 + a2 - a1 * a2, 0.0, 1.0);                 // ADDSMOOTH
    if (op == 12u) return a1 * diffuse.a + a2 * (1.0 - diffuse.a);             // BLENDDIFFUSEALPHA
    if (op == 13u) return a1 * texColor.a + a2 * (1.0 - texColor.a);           // BLENDTEXTUREALPHA
    if (op == 14u) return a1 * tfactor.a + a2 * (1.0 - tfactor.a);             // BLENDFACTORALPHA
    if (op == 15u) return a1 + a2 * (1.0 - texColor.a);                        // BLENDTEXTUREALPHAPM
    if (op == 16u) return a1 * current.a + a2 * (1.0 - current.a);             // BLENDCURRENTALPHA
    return a1 * a2;
}

void main() {
    vec4 diffuse = fragColor;

    // Replace vertex diffuse with timecycle color for world geometry (flag bit 4 = 16)
    if ((pc.flags & 16u) != 0u) {
        diffuse = vec4(
            float((pc.timecycleColor >> 16u) & 0xFFu) / 255.0,
            float((pc.timecycleColor >>  8u) & 0xFFu) / 255.0,
            float( pc.timecycleColor         & 0xFFu) / 255.0,
            diffuse.a
        );
    }

    vec4 tfactor = vec4(
        float((pc.textureFactor >> 16u) & 0xFFu) / 255.0,
        float((pc.textureFactor >>  8u) & 0xFFu) / 255.0,
        float( pc.textureFactor         & 0xFFu) / 255.0,
        float((pc.textureFactor >> 24u) & 0xFFu) / 255.0
    );

    uint tc0 = pc.texCoordIdx & 0xFFFFu;
    uint tc1 = pc.texCoordIdx >> 16u;

    bool hasTex0 = (pc.flags & 1u) != 0u;
    vec4 tex0 = hasTex0 ? texture(texSampler, getUV(tc0)) : vec4(1.0);

    // Color stage 0
    vec4 color;
    uint op0 = pc.stage0ColorOp;
    if (op0 <= 1u)
        color = diffuse;
    else
        color = runStage(op0, pc.stage0Args, diffuse, diffuse, tex0, tfactor);

    // Color stage 1
    bool hasTex1 = (pc.flags & 4u) != 0u;
    uint op1 = pc.stage1ColorOp;
    if (hasTex1 && op1 > 1u) {
        vec4 tex1 = texture(texSampler2, getUV(tc1));
        float prevAlpha = color.a;
        color = runStage(op1, pc.stage1Args, color, diffuse, tex1, tfactor);
        color.a = prevAlpha;
    }

    // Alpha stage 0 (separate from color stage)
    uint alphaOp = pc.stage0AlphaOp;
    if (alphaOp > 1u)
        color.a = runStage(alphaOp, pc.stage0AlphaArgs, diffuse, diffuse, tex0, tfactor).a;

    // Alpha test
    if ((pc.flags & 2u) != 0u) {
        if (color.a < pc.alphaRef) discard;
    }
    outColor = color;
}
