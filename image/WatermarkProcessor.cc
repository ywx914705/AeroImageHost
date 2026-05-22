/*
 * WatermarkProcessor.cc - 高性能水印处理实现
 *
 * 技术方案：
 *   1. 使用 Cairo 渲染文字为透明 PNG
 *   2. 使用 libvips 将水印 PNG 合成到原图
 *   3. 支持 9 宫格位置、透明度、字体大小、颜色
 */
#include "WatermarkProcessor.hpp"
#include <vips/vips.h>
#include <cairo/cairo.h>
#include <cstring>
#include <cmath>
#include <atomic>
#include <fontconfig/fontconfig.h>
#include "Log.hpp"

bool WatermarkProcessor::initialized = false;
static std::atomic<bool> fcInitialized(false);

static std::string findFontForText(const std::string& text) {
    if (!fcInitialized.load()) {
        if (FcInit()) {
            fcInitialized.store(true);
        } else {
            return "Noto Sans CJK SC, SimHei, SimSun, Microsoft YaHei, WenQuanYi Micro Hei, Sans";
        }
    }
    
    for (const char* fontName : {"Noto Sans CJK SC", "Noto Sans CJK TC", "Noto Sans CJK JP", 
                                  "SimHei", "SimSun", "Microsoft YaHei", "WenQuanYi Micro Hei"}) {
        FcPattern* pattern = FcPatternCreate();
        FcPatternAddString(pattern, FC_FAMILY, reinterpret_cast<const FcChar8*>(fontName));
        FcConfig* config = FcConfigGetCurrent();
        FcResult result;
        FcPattern* font = FcFontMatch(config, pattern, &result);
        if (font) {
            FcChar8* family = nullptr;
            if (FcPatternGetString(font, FC_FAMILY, 0, &family) == FcResultMatch && family) {
                std::string name(reinterpret_cast<char*>(family));
                FcPatternDestroy(font);
                FcPatternDestroy(pattern);
                return name;
            }
            FcPatternDestroy(font);
        }
        FcPatternDestroy(pattern);
    }
    
    return "Sans";
}

bool WatermarkProcessor::initialize() {
    if (initialized) return true;
    FcInit();
    initialized = true;
    LOG_INFO("WatermarkProcessor initialized");
    return true;
}

void WatermarkProcessor::cleanup() {
    initialized = false;
}

WatermarkPosition WatermarkProcessor::parsePosition(const std::string& pos) {
    if (pos == "top-left") return WatermarkPosition::TOP_LEFT;
    if (pos == "top-center") return WatermarkPosition::TOP_CENTER;
    if (pos == "top-right") return WatermarkPosition::TOP_RIGHT;
    if (pos == "middle-left") return WatermarkPosition::MIDDLE_LEFT;
    if (pos == "center") return WatermarkPosition::CENTER;
    if (pos == "middle-right") return WatermarkPosition::MIDDLE_RIGHT;
    if (pos == "bottom-left") return WatermarkPosition::BOTTOM_LEFT;
    if (pos == "bottom-center") return WatermarkPosition::BOTTOM_CENTER;
    return WatermarkPosition::BOTTOM_RIGHT;
}

bool WatermarkProcessor::isImageFormat(const std::string& mimeType) {
    return mimeType.find("image/") == 0;
}

// 解析 hex 颜色为 RGB
static void parseHexColor(const std::string& hex, double& r, double& g, double& b) {
    r = g = b = 1.0; // 默认白色
    if (hex.empty()) return;
    std::string h = hex;
    if (h[0] == '#') h = h.substr(1);
    // 支持 3 位和 6 位 hex
    if (h.length() == 3) {
        h = std::string(2, h[0]) + std::string(2, h[1]) + std::string(2, h[2]);
    }
    if (h.length() != 6) return;
    try {
        r = std::stoi(h.substr(0, 2), nullptr, 16) / 255.0;
        g = std::stoi(h.substr(2, 2), nullptr, 16) / 255.0;
        b = std::stoi(h.substr(4, 2), nullptr, 16) / 255.0;
    } catch (...) {
        r = g = b = 1.0;
    }
}

bool WatermarkProcessor::renderTextToPNG(const std::string& text, 
                                         int fontSize,
                                         const std::string& fontColor,
                                         int opacity,
                                         std::vector<unsigned char>& pngData,
                                         int& textWidth,
                                         int& textHeight) {
    std::string fontName = findFontForText(text);

    int surfaceWidth = fontSize * text.length() * 2;
    int surfaceHeight = fontSize * 2;

    cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, surfaceWidth, surfaceHeight);
    if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        AERO_LOG_ERROR("[Watermark] Failed to create Cairo surface");
        return false;
    }
    cairo_t* cr = cairo_create(surface);
    if (!cr) {
        cairo_surface_destroy(surface);
        AERO_LOG_ERROR("[Watermark] Failed to create Cairo context");
        return false;
    }

    cairo_set_source_rgba(cr, 0, 0, 0, 0);
    cairo_paint(cr);

    cairo_select_font_face(cr, fontName.c_str(), CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, fontSize);

    cairo_text_extents_t extents;
    cairo_text_extents(cr, text.c_str(), &extents);
    textWidth = static_cast<int>(extents.width + extents.x_bearing);
    textHeight = static_cast<int>(extents.height + extents.y_bearing);

    cairo_destroy(cr);
    cairo_surface_destroy(surface);

    surfaceWidth = textWidth + 20;
    surfaceHeight = textHeight + 20;

    surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, surfaceWidth, surfaceHeight);
    if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        AERO_LOG_ERROR("[Watermark] Failed to create final Cairo surface");
        return false;
    }
    cr = cairo_create(surface);
    if (!cr) {
        cairo_surface_destroy(surface);
        AERO_LOG_ERROR("[Watermark] Failed to create final Cairo context");
        return false;
    }

    cairo_set_source_rgba(cr, 0, 0, 0, 0);
    cairo_paint(cr);

    cairo_select_font_face(cr, fontName.c_str(), CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, fontSize);

    double r, g, b;
    parseHexColor(fontColor, r, g, b);
    double alpha = opacity / 100.0;

    // 先绘制黑色描边（增强任何背景下的可见性）
    cairo_set_source_rgba(cr, 0, 0, 0, alpha * 0.8);
    cairo_set_line_width(cr, fontSize * 0.15);
    cairo_move_to(cr, 10, surfaceHeight - 10);
    cairo_text_path(cr, text.c_str());
    cairo_stroke(cr);

    // 设置文字颜色
    cairo_set_source_rgba(cr, r, g, b, alpha);

    // 绘制文字
    cairo_move_to(cr, 10, surfaceHeight - 10);
    cairo_show_text(cr, text.c_str());

    // 输出为 PNG
    cairo_surface_flush(surface);

    // 获取像素数据
    unsigned char* data = cairo_image_surface_get_data(surface);
    int stride = cairo_image_surface_get_stride(surface);
    int height = cairo_image_surface_get_height(surface);
    int width = cairo_image_surface_get_width(surface);

    // 转换为 PNG 格式（使用 libvips）
    VipsImage* vipsImg = vips_image_new_from_memory(data, stride * height, width, height, 4, VIPS_FORMAT_UCHAR);
    if (!vipsImg) {
        cairo_destroy(cr);
        cairo_surface_destroy(surface);
        return false;
    }
    
    // 预乘 alpha（Cairo 使用预乘 alpha，需要转换）
    VipsImage* unpremultiplied = nullptr;
    if (vips_unpremultiply(vipsImg, &unpremultiplied, nullptr) != 0) {
        g_object_unref(vipsImg);
        cairo_destroy(cr);
        cairo_surface_destroy(surface);
        return false;
    }
    g_object_unref(vipsImg);
    
    // 转回 8-bit（unpremultiply 后变为 16-bit）
    VipsImage* eightbit = nullptr;
    if (vips_cast(unpremultiplied, &eightbit, VIPS_FORMAT_UCHAR, nullptr) != 0) {
        g_object_unref(unpremultiplied);
        cairo_destroy(cr);
        cairo_surface_destroy(surface);
        return false;
    }
    g_object_unref(unpremultiplied);
    
    // 保存为 PNG 到内存
    void* buf = nullptr;
    size_t len = 0;
    if (vips_image_write_to_buffer(eightbit, ".png", &buf, &len, nullptr) != 0) {
        g_object_unref(eightbit);
        cairo_destroy(cr);
        cairo_surface_destroy(surface);
        return false;
    }
    
    pngData.resize(len);
    memcpy(pngData.data(), buf, len);
    g_free(buf);
    g_object_unref(eightbit);
    
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
    
    return true;
}

void WatermarkProcessor::calculatePosition(int imgW, int imgH, 
                                           int wmW, int wmH, 
                                           WatermarkPosition pos, 
                                           int margin,
                                           int& x, int& y) {
    switch (pos) {
        case WatermarkPosition::TOP_LEFT:
            x = margin;
            y = margin;
            break;
        case WatermarkPosition::TOP_CENTER:
            x = (imgW - wmW) / 2;
            y = margin;
            break;
        case WatermarkPosition::TOP_RIGHT:
            x = imgW - wmW - margin;
            y = margin;
            break;
        case WatermarkPosition::MIDDLE_LEFT:
            x = margin;
            y = (imgH - wmH) / 2;
            break;
        case WatermarkPosition::CENTER:
            x = (imgW - wmW) / 2;
            y = (imgH - wmH) / 2;
            break;
        case WatermarkPosition::MIDDLE_RIGHT:
            x = imgW - wmW - margin;
            y = (imgH - wmH) / 2;
            break;
        case WatermarkPosition::BOTTOM_LEFT:
            x = margin;
            y = imgH - wmH - margin;
            break;
        case WatermarkPosition::BOTTOM_CENTER:
            x = (imgW - wmW) / 2;
            y = imgH - wmH - margin;
            break;
        case WatermarkPosition::BOTTOM_RIGHT:
        default:
            x = imgW - wmW - margin;
            y = imgH - wmH - margin;
            break;
    }
    
    // 边界检查
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x + wmW > imgW) x = imgW - wmW;
    if (y + wmH > imgH) y = imgH - wmH;
}

bool WatermarkProcessor::compositeWatermark(const std::vector<unsigned char>& src,
                                            const std::vector<unsigned char>& watermarkPng,
                                            std::vector<unsigned char>& dst,
                                            WatermarkPosition position,
                                            int margin) {
    // 加载原图
    VipsImage* base = vips_image_new_from_buffer(src.data(), src.size(), nullptr, nullptr);
    if (!base) {
        vips_error_clear();
        return false;
    }

    // 加载水印
    VipsImage* watermark = vips_image_new_from_buffer(watermarkPng.data(), watermarkPng.size(), nullptr, nullptr);
    if (!watermark) {
        g_object_unref(base);
        vips_error_clear();
        return false;
    }

    int imgW = vips_image_get_width(base);
    int imgH = vips_image_get_height(base);
    int wmW = vips_image_get_width(watermark);
    int wmH = vips_image_get_height(watermark);

    // 计算位置
    int x, y;
    calculatePosition(imgW, imgH, wmW, wmH, position, margin, x, y);

    // 确保原图有 Alpha 通道
    int baseBands = vips_image_get_bands(base);
    int wmBands = vips_image_get_bands(watermark);
    if (baseBands < wmBands) {
        VipsImage* withAlpha = nullptr;
        if (vips_addalpha(base, &withAlpha, nullptr) != 0) {
            g_object_unref(base);
            g_object_unref(watermark);
            vips_error_clear();
            return false;
        }
        g_object_unref(base);
        base = withAlpha;
    }
    // 确保格式匹配
    if (vips_image_get_format(watermark) != VIPS_FORMAT_UCHAR) {
        VipsImage* casted = nullptr;
        if (vips_cast(watermark, &casted, VIPS_FORMAT_UCHAR, nullptr) != 0) {
            g_object_unref(base);
            g_object_unref(watermark);
            vips_error_clear();
            return false;
        }
        g_object_unref(watermark);
        watermark = casted;
    }

    // 合成水印（使用 over 模式）
    VipsImage* result = nullptr;

    if (vips_composite2(base, watermark, &result, VIPS_BLEND_MODE_OVER,
                        "x", x, "y", y, nullptr) != 0) {
        g_object_unref(base);
        g_object_unref(watermark);
        vips_error_clear();
        return false;
    }

    // 输出为 JPEG
    void* buf = nullptr;
    size_t len = 0;
    if (vips_image_write_to_buffer(result, ".jpg", &buf, &len, 
                                    "Q", 90, nullptr) != 0) {
        g_object_unref(base);
        g_object_unref(watermark);
        g_object_unref(result);
        vips_error_clear();
        return false;
    }

    dst.resize(len);
    memcpy(dst.data(), buf, len);
    g_free(buf);

    g_object_unref(base);
    g_object_unref(watermark);
    g_object_unref(result);

    return true;
}

bool WatermarkProcessor::addTextWatermark(const std::vector<unsigned char>& src, 
                                           std::vector<unsigned char>& dst,
                                           const WatermarkConfig& config) {
    if (src.empty() || config.text.empty()) return false;
    
    // 渲染文字为透明 PNG
    std::vector<unsigned char> watermarkPng;
    int textWidth, textHeight;
    if (!renderTextToPNG(config.text, config.fontSize, config.fontColor, 
                         config.opacity, watermarkPng, textWidth, textHeight)) {
        LOG_ERROR("Failed to render watermark text");
        return false;
    }
    
    // 合成水印
    if (!compositeWatermark(src, watermarkPng, dst, config.position, config.margin)) {
        LOG_ERROR("Failed to composite watermark");
        return false;
    }
    
    LOG_INFO("Watermark applied: " + config.text);
    return true;
}

bool WatermarkProcessor::addTextWatermark(const std::vector<unsigned char>& src, 
                                           std::vector<unsigned char>& dst,
                                           const std::string& text,
                                           const std::string& position,
                                           int opacity) {
    WatermarkConfig config;
    config.text = text;
    config.position = parsePosition(position);
    config.opacity = opacity;
    return addTextWatermark(src, dst, config);
}
