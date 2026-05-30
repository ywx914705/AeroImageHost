/*
 * ImageProcessor 模块 - 图像处理实现
 *
 * 职责：基于 libvips 库提供图像处理功能。
 *
 * 核心功能：
 *   - getImageSize(): 从内存中的图片数据读取宽高
 *   - generateThumbnail(): 将图片按指定最大宽高缩放，输出为 JPEG 格式
 *
 * 在项目中的作用：
 *   - 客户端请求 /api/i/{id}?w=200&h=200 时调用 generateThumbnail()
 *   - 生成的缩略图异步缓存到 MinIO thumbs/ 前缀
 *   - 后续相同尺寸的请求直接从 MinIO 缓存读取
 *
 * 设计：所有方法为静态方法。libvips 仅初始化一次（通过 ensureVips() 懒加载）。
 *       libvips 比 ImageMagick 内存占用减少 90%，处理速度快 10 倍。
 */
#include "ImageProcessor.hpp"
#include <vips/vips.h>
#include <cstring>
#include "Log.hpp"

static bool vips_initialized = false;

static bool ensureVips() {
    if (!vips_initialized) {
        if (vips_init("image-host")) {
            LOG_ERROR("Failed to initialize libvips");
            return false;
        }
        vips_initialized = true;
    }
    return true;
}

bool ImageProcessor::getImageSize(const std::vector<char>& data, int& width, int& height) {
    if (data.empty()) return false;
    return getImageSize(reinterpret_cast<const unsigned char*>(data.data()), data.size(), width, height);
}

bool ImageProcessor::getImageSize(const unsigned char* data, size_t len, int& width, int& height) {
    if (!ensureVips()) return false;
    if (!data || len == 0) return false;

    VipsImage* img = vips_image_new_from_buffer(
        reinterpret_cast<const void*>(data), len, nullptr, nullptr);

    if (img == nullptr) {
        const char* err = vips_error_buffer();
        if (err && strlen(err) > 0) {
            LOG_ERROR("vips_image_new_from_buffer failed: " + std::string(err));
            vips_error_clear();
        }
        return false;
    }

    width = img->Xsize;
    height = img->Ysize;
    g_object_unref(img);
    return true;
}

bool ImageProcessor::generateThumbnail(const std::vector<char>& src, std::vector<char>& dst,
                                       int maxWidth, int maxHeight) {
    if (!ensureVips()) return false;
    if (src.empty()) {
        LOG_ERROR("generateThumbnail: source data empty");
        return false;
    }
    
    VipsImage* in = vips_image_new_from_buffer(src.data(), src.size(), nullptr, nullptr);
    if (in == nullptr) {
        const char* err = vips_error_buffer();
        LOG_ERROR("generateThumbnail: vips_image_new_from_buffer failed: " + 
                  (err ? std::string(err) : "unknown error"));
        vips_error_clear();
        return false;
    }
    
    VipsImage* out = nullptr;
    if (vips_thumbnail_image(in, &out, maxWidth, "height", maxHeight, nullptr)) {
        LOG_ERROR("vips_thumbnail_image failed");
        g_object_unref(in);
        return false;
    }
    
    void* buf = nullptr;
    size_t len = 0;
    if (vips_image_write_to_buffer(out, ".jpg", &buf, &len, "Q", 85, nullptr)) {
        LOG_ERROR("vips_image_write_to_buffer failed");
        g_object_unref(in);
        g_object_unref(out);
        return false;
    }
    
    dst.resize(len);
    memcpy(dst.data(), buf, len);
    g_free(buf);
    g_object_unref(in);
    g_object_unref(out);
    return true;
}