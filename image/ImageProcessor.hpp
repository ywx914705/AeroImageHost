#ifndef IMAGEPROCESSOR_HPP
#define IMAGEPROCESSOR_HPP

#include <vector>

class ImageProcessor {
public:
    static bool getImageSize(const std::vector<char>& data, int& width, int& height);
    static bool generateThumbnail(const std::vector<char>& src, std::vector<char>& dst,
                                  int maxWidth, int maxHeight);
};

#endif