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
    if (!ensureVips()) return false;
    
    if (data.empty()) {
        LOG_ERROR("getImageSize: input data is empty");
        return false;
    }
    
    // 注意：vips_image_new_from_buffer 返回 VipsImage*，失败时返回 NULL
    VipsImage* img = vips_image_new_from_buffer(
        reinterpret_cast<const void*>(data.data()),
        data.size(),
        nullptr,   // option_string
        nullptr    // 参数列表结束标志
    );
    
    if (img == nullptr) {
        // 获取 libvips 错误信息并记录
        const char* err = vips_error_buffer();
        if (err && strlen(err) > 0) {
            LOG_ERROR("vips_image_new_from_buffer failed: " + std::string(err));
            vips_error_clear();
        } else {
            LOG_ERROR("vips_image_new_from_buffer failed, data size: " + std::to_string(data.size()));
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
    if (vips_image_write_to_buffer(out, ".jpg", &buf, &len, nullptr)) {
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