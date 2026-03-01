#include "TextureUtils.h"
#include "../Debug.h"
#include <cstring>

namespace gvlk {
namespace texture {

namespace {

unsigned char extractChannel(DWORD pixel, DWORD mask) {
    if (mask == 0)
        return 255;
    int shift = 0;
    DWORD m = mask;
    while (m && !(m & 1)) { shift++; m >>= 1; }
    int bits = 0;
    while (m & 1) { bits++; m >>= 1; }
    if (bits == 0)
        return 255;
    DWORD value = (pixel & mask) >> shift;
    return (unsigned char)(value * 255 / ((1 << bits) - 1));
}

DWORD bppFromMasks(const DDPIXELFORMAT& pf) {
    DWORD allMasks = pf.dwRBitMask | pf.dwGBitMask | pf.dwBBitMask | pf.dwRGBAlphaBitMask;
    if (allMasks == 0)
        return 0;
    int highBit = 0;
    while (allMasks >> highBit)
        highBit++;
    return (highBit <= 8) ? 8 : (highBit <= 16) ? 16 : 32;
}

} // namespace

uint32_t calcMipLevels(uint32_t w, uint32_t h) {
    uint32_t levels = 1;
    uint32_t dim   = (w > h) ? w : h;
    while (dim > 1) { dim >>= 1; levels++; }
    return levels;
}

void convertToRgba(const void* src, std::vector<unsigned char>& dst,
                   DWORD w, DWORD h, DWORD pitch, const DDPIXELFORMAT& fmt) {
    dst.resize(w * h * 4);
    DWORD bpp = fmt.dwRGBBitCount;
    if (bpp == 0) {
        bpp = bppFromMasks(fmt);
        if (bpp == 0)
            bpp = 32;
    }
    for (DWORD y = 0; y < h; y++) {
        const unsigned char* row = (const unsigned char*)src + y * pitch;
        for (DWORD x = 0; x < w; x++) {
            DWORD pixel = 0;
            if (bpp == 32)
                pixel = *(const DWORD*)(row + x * 4);
            else if (bpp == 16)
                pixel = *(const unsigned short*)(row + x * 2);
            else if (bpp == 24)
                pixel = row[x*3] | (row[x*3+1] << 8) | (row[x*3+2] << 16);
            DWORD idx = (y * w + x) * 4;
            dst[idx + 0] = extractChannel(pixel, fmt.dwRBitMask);
            dst[idx + 1] = extractChannel(pixel, fmt.dwGBitMask);
            dst[idx + 2] = extractChannel(pixel, fmt.dwBBitMask);
            dst[idx + 3] = (fmt.dwFlags & DDPF_ALPHAPIXELS) ? extractChannel(pixel, fmt.dwRGBAlphaBitMask) : 255;
        }
    }
}

VkFormat fourCCToVkFormat(DWORD fourCC) {
    switch (fourCC) {
    case MAKEFOURCC('D','X','T','1'): return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
    case MAKEFOURCC('D','X','T','3'): return VK_FORMAT_BC2_UNORM_BLOCK;
    case MAKEFOURCC('D','X','T','5'): return VK_FORMAT_BC3_UNORM_BLOCK;
    default: return VK_FORMAT_UNDEFINED;
    }
}

DWORD fourCCBlockSize(DWORD fourCC) {
    if (fourCC == MAKEFOURCC('D','X','T','1'))
        return 8;
    return 16;
}

void transitionImageLayout(VkCommandBuffer cmd, VkImage image,
                           VkImageLayout oldLayout, VkImageLayout newLayout,
                           uint32_t baseMip, uint32_t mipCount) {
    VkImageMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel   = baseMip;
    barrier.subresourceRange.levelCount    = mipCount;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount     = 1;

    VkPipelineStageFlags srcStage, dstStage;
    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        srcStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }
    vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

void uploadImageData(VkImage image, DWORD w, DWORD h, uint32_t mipLevel,
                     const void* data, DWORD dataSize,
                     VkDevice device, VkBuffer stagingBuffer, void* stagingMapped,
                     VmaAllocator, VkCommandPool cmdPool, VkQueue queue) {
    const uint32_t STAGING_MAX = 16 * 1024 * 1024;
    if (dataSize > STAGING_MAX)
        return;
    memcpy(stagingMapped, data, dataSize);

    VkCommandBufferAllocateInfo ai = {};
    ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool = cmdPool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(device, &ai, &cmd);

    VkCommandBufferBeginInfo bi = {};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);

    transitionImageLayout(cmd, image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, mipLevel, 1);

    VkBufferImageCopy region = {};
    region.bufferOffset = 0;
    region.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel       = mipLevel;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount     = 1;
    region.imageExtent = { w, h, 1 };
    vkCmdCopyBufferToImage(cmd, stagingBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    transitionImageLayout(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, mipLevel, 1);

    vkEndCommandBuffer(cmd);
    VkSubmitInfo si = {};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue);
    vkFreeCommandBuffers(device, cmdPool, 1, &cmd);
}

void generateMipmaps(VkImage image, uint32_t w, uint32_t h, uint32_t mipLevels, uint32_t fromLevel,
                     VkDevice device, VkCommandPool cmdPool, VkQueue queue) {
    if (fromLevel + 1 >= mipLevels)
        return;

    VkCommandBufferAllocateInfo ai = {};
    ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool = cmdPool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(device, &ai, &cmd);

    VkCommandBufferBeginInfo bi = {};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);

    int32_t mipW = (int32_t)(w >> fromLevel);
    int32_t mipH = (int32_t)(h >> fromLevel);
    if (mipW < 1) mipW = 1;
    if (mipH < 1) mipH = 1;

    for (uint32_t i = fromLevel; i < mipLevels - 1; i++) {
        transitionImageLayout(cmd, image,
            (i == fromLevel) ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, i, 1);
        transitionImageLayout(cmd, image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, i + 1, 1);

        int32_t nextW = mipW > 1 ? mipW / 2 : 1;
        int32_t nextH = mipH > 1 ? mipH / 2 : 1;

        VkImageBlit blit = {};
        blit.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, i, 0, 1 };
        blit.srcOffsets[0] = { 0, 0, 0 };
        blit.srcOffsets[1] = { mipW, mipH, 1 };
        blit.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, i + 1, 0, 1 };
        blit.dstOffsets[0] = { 0, 0, 0 };
        blit.dstOffsets[1] = { nextW, nextH, 1 };
        vkCmdBlitImage(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);

        transitionImageLayout(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, i, 1);
        mipW = nextW;
        mipH = nextH;
    }

    transitionImageLayout(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, mipLevels - 1, 1);

    vkEndCommandBuffer(cmd);
    VkSubmitInfo si = {};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue);
    vkFreeCommandBuffers(device, cmdPool, 1, &cmd);
}

VkSampler createSampler(VkDevice device, uint32_t mipLevels) {
    VkSamplerCreateInfo sci = {};
    sci.sType         = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sci.magFilter     = VK_FILTER_LINEAR;
    sci.minFilter     = VK_FILTER_LINEAR;
    sci.mipmapMode    = mipLevels > 1 ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sci.addressModeU  = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sci.addressModeV  = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sci.addressModeW  = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sci.maxLod        = (float)mipLevels;
    sci.anisotropyEnable = (mipLevels > 1) ? VK_TRUE : VK_FALSE;
    sci.maxAnisotropy   = (mipLevels > 1) ? 16.0f : 1.0f;
    VkSampler s = VK_NULL_HANDLE;
    vkCreateSampler(device, &sci, nullptr, &s);
    return s;
}

VkDescriptorSet allocateDescriptorSet(VkDevice device, VkDescriptorPool pool, VkDescriptorSetLayout layout) {
    VkDescriptorSetAllocateInfo ai = {};
    ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool = pool;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts = &layout;
    VkDescriptorSet set = VK_NULL_HANDLE;
    if (vkAllocateDescriptorSets(device, &ai, &set) != VK_SUCCESS)
        return VK_NULL_HANDLE;
    return set;
}

void updateDescriptorSet(VkDevice device, VkDescriptorSet set, VkImageView view, VkSampler sampler) {
    VkDescriptorImageInfo imgInfo = {};
    imgInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imgInfo.imageView   = view;
    imgInfo.sampler     = sampler;
    VkWriteDescriptorSet write = {};
    write.sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet           = set;
    write.dstBinding       = 0;
    write.descriptorCount  = 1;
    write.descriptorType   = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo       = &imgInfo;
    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
}

} // namespace texture
} // namespace gvlk
