/*
 * ImageProcessor.hpp - 图像处理模块头文件
 *
 * 职责：基于 libvips 库提供图像处理功能，主要包括：
 *   - 获取图片尺寸（宽高）
 *   - 生成缩略图（按指定最大宽高缩放）
 *
 * 在项目中的作用：
 *   - 客户端请求 /api/i/{id}?w=200&h=200 时生成缩略图
 *   - 缩略图缓存到 MinIO thumbs/ 前缀，后续请求直接读缓存
 *   - 缓存写入通过 AeroQueue 异步执行，不阻塞响应
 *
 * 设计：所有方法为静态方法，无状态。libvips 仅初始化一次（懒加载）。
 */
#ifndef IMAGEPROCESSOR_HPP
#define IMAGEPROCESSOR_HPP

#include <vector>

class ImageProcessor {
public:
    // 获取图片尺寸：从内存中的图片数据读取宽高，失败返回 false
    static bool getImageSize(const std::vector<char>& data, int& width, int& height);
    // 生成缩略图：将 src 中的图片按 maxWidth × maxHeight 缩放，输出为 JPEG 格式到 dst
    static bool generateThumbnail(const std::vector<char>& src, std::vector<char>& dst,
                                  int maxWidth, int maxHeight);
};

#endif