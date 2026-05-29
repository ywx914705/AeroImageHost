/*
 * WatermarkProcessor.hpp - 高性能水印处理模块
 *
 * 设计目标：高性能、高并发、高可用
 *   - 异步生成：水印不在请求时实时生成
 *   - 缓存优先：生成后的水印图片缓存到 MinIO
 *   - 懒加载：只有被访问时才生成水印版本
 *   - 队列削峰：使用 AeroQueue 处理水印生成任务
 *
 * 技术方案：
 *   - 使用 libvips 进行图片合成（高性能）
 *   - 使用 Cairo 渲染文字水印（高质量）
 *   - 支持 9 宫格位置
 *   - 支持透明度调节
 */
#ifndef WATERMARKPROCESSOR_HPP
#define WATERMARKPROCESSOR_HPP

#include <vector>
#include <string>
#include <memory>

// 水印位置枚举
enum class WatermarkPosition {
    TOP_LEFT,      // 左上
    TOP_CENTER,    // 上中
    TOP_RIGHT,     // 右上
    MIDDLE_LEFT,   // 左中
    CENTER,        // 正中
    MIDDLE_RIGHT,  // 右中
    BOTTOM_LEFT,   // 左下
    BOTTOM_CENTER, // 下中
    BOTTOM_RIGHT   // 右下
};

// 水印配置结构
struct WatermarkConfig {
    std::string text;                    // 水印文字
    WatermarkPosition position;          // 位置
    int opacity;                         // 透明度 0-100
    int fontSize;                        // 字体大小
    std::string fontColor;               // 字体颜色 (hex)
    int margin;                          // 边距
    
    WatermarkConfig() 
        : position(WatermarkPosition::BOTTOM_RIGHT)
        , opacity(80)
        , fontSize(24)
        , fontColor("#FFFFFF")
        , margin(20) {}
};

class WatermarkProcessor {
public:
    // 初始化水印处理器（加载字体等）
    static bool initialize();
    // 清理资源
    static void cleanup();
    
    // 添加文字水印（同步版本，用于后台任务）
    static bool addTextWatermark(const std::vector<unsigned char>& src, 
                                 std::vector<unsigned char>& dst,
                                 const WatermarkConfig& config);
    
    // 添加文字水印（简化版本）
    static bool addTextWatermark(const std::vector<unsigned char>& src, 
                                 std::vector<unsigned char>& dst,
                                 const std::string& text,
                                 const std::string& position = "bottom-right",
                                 int opacity = 50);
    
    // 字符串位置转枚举
    static WatermarkPosition parsePosition(const std::string& pos);
    
    // 检查图片是否支持水印（仅图片格式）
    static bool isImageFormat(const std::string& mimeType);
    
private:
    // 使用 Cairo 渲染文字为透明 PNG
    static bool renderTextToPNG(const std::string& text, 
                                int fontSize,
                                const std::string& fontColor,
                                int opacity,
                                std::vector<unsigned char>& pngData,
                                int& textWidth,
                                int& textHeight);
    
    // 使用 libvips 合成水印
    static bool compositeWatermark(const std::vector<unsigned char>& src,
                                   const std::vector<unsigned char>& watermarkPng,
                                   std::vector<unsigned char>& dst,
                                   WatermarkPosition position,
                                   int margin);
    
    // 计算水印位置
    static void calculatePosition(int imgW, int imgH, 
                                  int wmW, int wmH, 
                                  WatermarkPosition pos, 
                                  int margin,
                                  int& x, int& y);
    
    static bool initialized;
};

#endif
