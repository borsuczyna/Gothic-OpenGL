#include "gl_renderer.h"
#include "../debug.h"
#include <vector>
#include <cstring>

typedef void (APIENTRY *PFNGLCOMPRESSEDTEXIMAGE2DPROC)(
    GLenum target, GLint level, GLenum internalformat,
    GLsizei width, GLsizei height, GLint border,
    GLsizei imageSize, const void* data);

static PFNGLCOMPRESSEDTEXIMAGE2DPROC pfnCompressedTexImage2D = nullptr;

namespace GLRenderer {

static bool s_initialized = false;
static int s_windowW = 800, s_windowH = 600;
static int s_gameW = 800, s_gameH = 600;

static float s_worldMatrix[16];
static float s_viewMatrix[16];
static float s_projMatrix[16];
static bool s_in2DMode = true;

static DWORD s_vpX = 0, s_vpY = 0, s_vpW = 800, s_vpH = 600;

static bool s_gameDepthEnabled = true;
static bool s_gameDepthWriteEnabled = true;
static DWORD s_gameDepthFunc = D3DCMP_LESSEQUAL;

static void MakeIdentity(float* m) {
    memset(m, 0, 16 * sizeof(float));
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

static void AdjustProjD3DToGL(float* glProj) {
    // D3D maps Z to [0,1], OpenGL to [-1,1].
    // z_clip_gl = 2*z_clip_d3d - w_clip
    // In GL column-major, z_clip is row 2 (indices 2,6,10,14),
    // w_clip is row 3 (indices 3,7,11,15).
    glProj[2]  = 2.0f * glProj[2]  - glProj[3];
    glProj[6]  = 2.0f * glProj[6]  - glProj[7];
    glProj[10] = 2.0f * glProj[10] - glProj[11];
    glProj[14] = 2.0f * glProj[14] - glProj[15];
}

static void SetupFor2D() {
    if (s_in2DMode) return;
    s_in2DMode = true;
    glViewport(0, 0, s_windowW, s_windowH);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, (double)s_gameW, (double)s_gameH, 0.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
}

static void ApplyViewport3D() {
    float sx = (float)s_windowW / (float)s_gameW;
    float sy = (float)s_windowH / (float)s_gameH;
    int glX = (int)(s_vpX * sx);
    int glW = (int)(s_vpW * sx);
    int glH = (int)(s_vpH * sy);
    int glY = s_windowH - (int)(s_vpY * sy) - glH;
    glViewport(glX, glY, glW, glH);
}

static GLenum D3DCmpToGL(DWORD d3dFunc);

static void SetupFor3D() {
    if (!s_in2DMode) return;
    s_in2DMode = false;

    ApplyViewport3D();

    float proj[16];
    memcpy(proj, s_projMatrix, sizeof(proj));
    AdjustProjD3DToGL(proj);

    glMatrixMode(GL_PROJECTION);
    glLoadMatrixf(proj);

    glMatrixMode(GL_MODELVIEW);
    glLoadMatrixf(s_viewMatrix);
    glMultMatrixf(s_worldMatrix);

    if (s_gameDepthEnabled) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    glDepthMask(s_gameDepthWriteEnabled ? GL_TRUE : GL_FALSE);
    glDepthFunc(D3DCmpToGL(s_gameDepthFunc));
}

void Init() {
    if (s_initialized) return;

    pfnCompressedTexImage2D = (PFNGLCOMPRESSEDTEXIMAGE2DPROC)
        wglGetProcAddress("glCompressedTexImage2D");

    glEnable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_LIGHTING);
    glDisable(GL_CULL_FACE);

    MakeIdentity(s_worldMatrix);
    MakeIdentity(s_viewMatrix);
    MakeIdentity(s_projMatrix);

    s_initialized = true;
    DbgPrint("GLRenderer initialized (compressed tex support: %s)",
             pfnCompressedTexImage2D ? "yes" : "no");
}

void Shutdown() {
    s_initialized = false;
}

void BeginFrame(int windowW, int windowH, int gameW, int gameH) {
    s_windowW = windowW;
    s_windowH = windowH;
    s_gameW = gameW;
    s_gameH = gameH;

    glViewport(0, 0, windowW, windowH);

    s_in2DMode = true;
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, (double)gameW, (double)gameH, 0.0, -1.0, 1.0);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glDisable(GL_LIGHTING);
    glEnable(GL_TEXTURE_2D);
}

void Clear(DWORD flags, D3DCOLOR color, float z, DWORD stencil) {
    GLbitfield mask = 0;
    if (flags & D3DCLEAR_TARGET) {
        float r = ((color >> 16) & 0xFF) / 255.0f;
        float g = ((color >> 8) & 0xFF) / 255.0f;
        float b = (color & 0xFF) / 255.0f;
        float a = ((color >> 24) & 0xFF) / 255.0f;
        glClearColor(r, g, b, a);
        mask |= GL_COLOR_BUFFER_BIT;
    }
    if (flags & D3DCLEAR_ZBUFFER) {
        glDepthMask(GL_TRUE);
        glClearDepth((double)z);
        mask |= GL_DEPTH_BUFFER_BIT;
    }
    if (flags & D3DCLEAR_STENCIL) {
        glClearStencil(stencil);
        mask |= GL_STENCIL_BUFFER_BIT;
    }
    if (mask) {
        glDisable(GL_SCISSOR_TEST);
        glClear(mask);
    }
}

// --- Pixel format helpers ---

static unsigned char ExtractChannel(DWORD pixel, DWORD mask) {
    if (mask == 0) return 255;
    int shift = 0;
    DWORD m = mask;
    while (m && !(m & 1)) { shift++; m >>= 1; }
    int bits = 0;
    while (m & 1) { bits++; m >>= 1; }
    if (bits == 0) return 255;
    DWORD value = (pixel & mask) >> shift;
    return (unsigned char)(value * 255 / ((1 << bits) - 1));
}

static DWORD BppFromMasks(const DDPIXELFORMAT& pf) {
    DWORD allMasks = pf.dwRBitMask | pf.dwGBitMask | pf.dwBBitMask | pf.dwRGBAlphaBitMask;
    if (allMasks == 0) return 0;
    int highBit = 0;
    while (allMasks >> highBit) highBit++;
    return (highBit <= 8) ? 8 : (highBit <= 16) ? 16 : 32;
}

static void ConvertToRGBA(const void* src, std::vector<unsigned char>& dst,
                          DWORD w, DWORD h, DWORD pitch, const DDPIXELFORMAT& fmt) {
    dst.resize(w * h * 4);
    DWORD bpp = fmt.dwRGBBitCount;
    if (bpp == 0) {
        bpp = BppFromMasks(fmt);
        if (bpp == 0) bpp = 32;
    }

    for (DWORD y = 0; y < h; y++) {
        const unsigned char* row = (const unsigned char*)src + y * pitch;
        for (DWORD x = 0; x < w; x++) {
            DWORD pixel = 0;
            if (bpp == 32)      pixel = *(const DWORD*)(row + x * 4);
            else if (bpp == 16) pixel = *(const unsigned short*)(row + x * 2);
            else if (bpp == 24) pixel = row[x*3] | (row[x*3+1] << 8) | (row[x*3+2] << 16);

            DWORD idx = (y * w + x) * 4;
            dst[idx + 0] = ExtractChannel(pixel, fmt.dwRBitMask);
            dst[idx + 1] = ExtractChannel(pixel, fmt.dwGBitMask);
            dst[idx + 2] = ExtractChannel(pixel, fmt.dwBBitMask);
            dst[idx + 3] = (fmt.dwFlags & DDPF_ALPHAPIXELS) ?
                ExtractChannel(pixel, fmt.dwRGBAlphaBitMask) : 255;
        }
    }
}

static bool UploadCompressed(GLuint texId, int level, DWORD w, DWORD h,
                             const void* data, const DDPIXELFORMAT& fmt) {
    if (!pfnCompressedTexImage2D) return false;
    if (!(fmt.dwFlags & DDPF_FOURCC)) return false;

    GLenum glFmt = 0;
    DWORD blockSize = 16;
    switch (fmt.dwFourCC) {
        case FOURCC_DXT1: glFmt = GL_COMPRESSED_RGBA_S3TC_DXT1_EXT; blockSize = 8; break;
        case FOURCC_DXT3: glFmt = GL_COMPRESSED_RGBA_S3TC_DXT3_EXT; break;
        case FOURCC_DXT5: glFmt = GL_COMPRESSED_RGBA_S3TC_DXT5_EXT; break;
        default: return false;
    }

    DWORD bw = (w + 3) / 4;
    DWORD bh = (h + 3) / 4;
    GLsizei size = bw * bh * blockSize;

    glBindTexture(GL_TEXTURE_2D, texId);
    pfnCompressedTexImage2D(GL_TEXTURE_2D, level, glFmt, w, h, 0, size, data);
    return true;
}

static void UploadUncompressed(GLuint texId, int level, DWORD w, DWORD h,
                               const void* data, DWORD pitch, const DDPIXELFORMAT& fmt) {
    std::vector<unsigned char> rgba;
    ConvertToRGBA(data, rgba, w, h, pitch, fmt);

    glBindTexture(GL_TEXTURE_2D, texId);
    glTexImage2D(GL_TEXTURE_2D, level, GL_RGBA, w, h, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
}

GLuint UploadTexture(DWORD w, DWORD h, const void* data, DWORD pitch, const DDPIXELFORMAT& fmt) {
    if (!data || w == 0 || h == 0) return 0;

    DbgPrint("UploadTexture %ux%u pitch=%u bpp=%u flags=0x%X fourcc=0x%X R=0x%X G=0x%X B=0x%X A=0x%X",
             w, h, pitch, fmt.dwRGBBitCount, fmt.dwFlags, fmt.dwFourCC,
             fmt.dwRBitMask, fmt.dwGBitMask, fmt.dwBBitMask, fmt.dwRGBAlphaBitMask);

    GLuint texId = 0;
    glGenTextures(1, &texId);
    glBindTexture(GL_TEXTURE_2D, texId);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);

    if (fmt.dwFlags & DDPF_FOURCC) {
        if (!UploadCompressed(texId, 0, w, h, data, fmt)) {
            glDeleteTextures(1, &texId);
            return 0;
        }
    } else {
        UploadUncompressed(texId, 0, w, h, data, pitch, fmt);
    }

    return texId;
}

void UpdateTexture(GLuint texId, DWORD w, DWORD h, const void* data, DWORD pitch, const DDPIXELFORMAT& fmt) {
    if (!texId || !data || w == 0 || h == 0) return;

    if (fmt.dwFlags & DDPF_FOURCC) {
        UploadCompressed(texId, 0, w, h, data, fmt);
    } else {
        UploadUncompressed(texId, 0, w, h, data, pitch, fmt);
    }
}

void UploadTextureMipLevel(GLuint texId, int level, DWORD w, DWORD h,
                           const void* data, DWORD pitch, const DDPIXELFORMAT& fmt) {
    if (!texId || !data || w == 0 || h == 0) return;

    if (fmt.dwFlags & DDPF_FOURCC) {
        UploadCompressed(texId, level, w, h, data, fmt);
    } else {
        UploadUncompressed(texId, level, w, h, data, pitch, fmt);
    }
}

void SetTextureMipmapParams(GLuint texId, int mipCount) {
    if (!texId || mipCount <= 0) return;
    glBindTexture(GL_TEXTURE_2D, texId);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, mipCount);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
}

void FreeTexture(GLuint texId) {
    if (texId) glDeleteTextures(1, &texId);
}

void BindTexture(GLuint texId) {
    if (texId) {
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, texId);
    } else {
        glDisable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
}

// --- Render state ---

static GLenum D3DBlendToGL(DWORD blend) {
    switch (blend) {
        case D3DBLEND_ZERO:         return GL_ZERO;
        case D3DBLEND_ONE:          return GL_ONE;
        case D3DBLEND_SRCCOLOR:     return GL_SRC_COLOR;
        case D3DBLEND_INVSRCCOLOR:  return GL_ONE_MINUS_SRC_COLOR;
        case D3DBLEND_SRCALPHA:     return GL_SRC_ALPHA;
        case D3DBLEND_INVSRCALPHA:  return GL_ONE_MINUS_SRC_ALPHA;
        case D3DBLEND_DESTALPHA:    return GL_DST_ALPHA;
        case D3DBLEND_INVDESTALPHA: return GL_ONE_MINUS_DST_ALPHA;
        case D3DBLEND_DESTCOLOR:    return GL_DST_COLOR;
        case D3DBLEND_INVDESTCOLOR: return GL_ONE_MINUS_DST_COLOR;
        default: return GL_ONE;
    }
}

void SetAlphaBlendEnabled(bool enabled) {
    if (enabled) glEnable(GL_BLEND); else glDisable(GL_BLEND);
}

void SetBlendFunc(DWORD srcBlend, DWORD dstBlend) {
    glBlendFunc(D3DBlendToGL(srcBlend), D3DBlendToGL(dstBlend));
}

void SetAlphaTestEnabled(bool enabled) {
    if (enabled) glEnable(GL_ALPHA_TEST); else glDisable(GL_ALPHA_TEST);
}

void SetAlphaRef(DWORD ref) {
    glAlphaFunc(GL_GEQUAL, ref / 255.0f);
}

void SetDepthEnabled(bool enabled) {
    s_gameDepthEnabled = enabled;
    if (enabled) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
}

void SetDepthWriteEnabled(bool enabled) {
    s_gameDepthWriteEnabled = enabled;
    glDepthMask(enabled ? GL_TRUE : GL_FALSE);
}

static GLenum D3DCmpToGL(DWORD d3dFunc) {
    switch (d3dFunc) {
        case D3DCMP_NEVER:        return GL_NEVER;
        case D3DCMP_LESS:         return GL_LESS;
        case D3DCMP_EQUAL:        return GL_EQUAL;
        case D3DCMP_LESSEQUAL:    return GL_LEQUAL;
        case D3DCMP_GREATER:      return GL_GREATER;
        case D3DCMP_NOTEQUAL:     return GL_NOTEQUAL;
        case D3DCMP_GREATEREQUAL: return GL_GEQUAL;
        case D3DCMP_ALWAYS:       return GL_ALWAYS;
        default: return GL_LEQUAL;
    }
}

void SetDepthFunc(DWORD d3dFunc) {
    s_gameDepthFunc = d3dFunc;
    glDepthFunc(D3DCmpToGL(d3dFunc));
}

void SetWorldMatrix(const float* m) {
    memcpy(s_worldMatrix, m, 16 * sizeof(float));
    if (!s_in2DMode) {
        glMatrixMode(GL_MODELVIEW);
        glLoadMatrixf(s_viewMatrix);
        glMultMatrixf(s_worldMatrix);
    }
}

void SetViewMatrix(const float* m) {
    memcpy(s_viewMatrix, m, 16 * sizeof(float));
    if (!s_in2DMode) {
        glMatrixMode(GL_MODELVIEW);
        glLoadMatrixf(s_viewMatrix);
        glMultMatrixf(s_worldMatrix);
    }
}

void SetProjectionMatrix(const float* m) {
    memcpy(s_projMatrix, m, 16 * sizeof(float));
    if (!s_in2DMode) {
        float proj[16];
        memcpy(proj, s_projMatrix, sizeof(proj));
        AdjustProjD3DToGL(proj);
        glMatrixMode(GL_PROJECTION);
        glLoadMatrixf(proj);
    }
}

void SetViewport(DWORD x, DWORD y, DWORD w, DWORD h) {
    s_vpX = x; s_vpY = y; s_vpW = w; s_vpH = h;
    if (!s_in2DMode) {
        ApplyViewport3D();
    }
}

#ifndef GL_MIRRORED_REPEAT
#define GL_MIRRORED_REPEAT 0x8370
#endif

static GLenum D3DAddrToGL(DWORD addr) {
    switch (addr) {
        case D3DTADDRESS_WRAP:       return GL_REPEAT;
        case D3DTADDRESS_MIRROR:     return GL_MIRRORED_REPEAT;
        case D3DTADDRESS_CLAMP:      return GL_CLAMP_TO_EDGE;
        default:                     return GL_REPEAT;
    }
}

void SetTextureAddressU(DWORD d3dAddr) {
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, D3DAddrToGL(d3dAddr));
}

void SetTextureAddressV(DWORD d3dAddr) {
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, D3DAddrToGL(d3dAddr));
}

// --- FVF helpers ---

static DWORD CalcFVFStride(DWORD fvf) {
    DWORD stride = 0;
    switch (fvf & D3DFVF_POSITION_MASK) {
        case D3DFVF_XYZ:    stride = 12; break;
        case D3DFVF_XYZRHW: stride = 16; break;
        case D3DFVF_XYZB1:  stride = 16; break;
        case D3DFVF_XYZB2:  stride = 20; break;
        case D3DFVF_XYZB3:  stride = 24; break;
        case D3DFVF_XYZB4:  stride = 28; break;
        case D3DFVF_XYZB5:  stride = 32; break;
    }
    if (fvf & D3DFVF_NORMAL)   stride += 12;
    if (fvf & D3DFVF_DIFFUSE)  stride += 4;
    if (fvf & D3DFVF_SPECULAR) stride += 4;
    stride += ((fvf & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT) * 8;
    return stride;
}

static GLenum D3DPrimToGL(D3DPRIMITIVETYPE type) {
    switch (type) {
        case D3DPT_POINTLIST:     return GL_POINTS;
        case D3DPT_LINELIST:      return GL_LINES;
        case D3DPT_LINESTRIP:     return GL_LINE_STRIP;
        case D3DPT_TRIANGLELIST:  return GL_TRIANGLES;
        case D3DPT_TRIANGLESTRIP: return GL_TRIANGLE_STRIP;
        case D3DPT_TRIANGLEFAN:   return GL_TRIANGLE_FAN;
        default: return GL_TRIANGLES;
    }
}

static void EmitVertex(const unsigned char* ptr, DWORD fvf, DWORD stride) {
    DWORD off = 0;

    float x = *(float*)(ptr + off); off += 4;
    float y = *(float*)(ptr + off); off += 4;
    float z = *(float*)(ptr + off); off += 4;

    bool isRHW = (fvf & D3DFVF_POSITION_MASK) == D3DFVF_XYZRHW;
    if (isRHW) off += 4; // skip rhw

    if (fvf & D3DFVF_NORMAL) off += 12;

    unsigned char r = 255, g = 255, b = 255, a = 255;
    if (fvf & D3DFVF_DIFFUSE) {
        DWORD color = *(DWORD*)(ptr + off); off += 4;
        a = (color >> 24) & 0xFF;
        r = (color >> 16) & 0xFF;
        g = (color >> 8)  & 0xFF;
        b = color & 0xFF;
    }

    if (fvf & D3DFVF_SPECULAR) off += 4;

    DWORD texCount = (fvf & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT;
    float u = 0, v = 0;
    if (texCount >= 1) {
        u = *(float*)(ptr + off); off += 4;
        v = *(float*)(ptr + off); off += 4;
    }

    glColor4ub(r, g, b, a);
    if (texCount >= 1) glTexCoord2f(u, v);
    glVertex3f(x, y, z);
}

void DrawPrimitive(D3DPRIMITIVETYPE type, DWORD fvf, const void* vertices, DWORD count) {
    if (!vertices || count == 0) return;

    bool isRHW = (fvf & D3DFVF_POSITION_MASK) == D3DFVF_XYZRHW;
    if (isRHW) SetupFor2D(); else SetupFor3D();

    DWORD stride = CalcFVFStride(fvf);
    GLenum glType = D3DPrimToGL(type);

    glBegin(glType);
    const unsigned char* ptr = (const unsigned char*)vertices;
    for (DWORD i = 0; i < count; i++) {
        EmitVertex(ptr, fvf, stride);
        ptr += stride;
    }
    glEnd();
}

void DrawIndexedPrimitive(D3DPRIMITIVETYPE type, DWORD fvf, const void* vertices,
                          DWORD vertexCount, const WORD* indices, DWORD indexCount) {
    if (!vertices || !indices || indexCount == 0) return;

    bool isRHW = (fvf & D3DFVF_POSITION_MASK) == D3DFVF_XYZRHW;
    if (isRHW) SetupFor2D(); else SetupFor3D();

    DWORD stride = CalcFVFStride(fvf);
    GLenum glType = D3DPrimToGL(type);

    glBegin(glType);
    for (DWORD i = 0; i < indexCount; i++) {
        WORD idx = indices[i];
        if (idx >= vertexCount) continue;
        const unsigned char* ptr = (const unsigned char*)vertices + idx * stride;
        EmitVertex(ptr, fvf, stride);
    }
    glEnd();
}

} // namespace GLRenderer
