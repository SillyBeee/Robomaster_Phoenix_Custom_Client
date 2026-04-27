#include "utils_cv.hpp"

slint::Image MatToSlintImage(const cv::Mat &mat)
{
    cv::Mat rgb;
    if (mat.type() == CV_8UC3) {
        cv::cvtColor(mat, rgb, cv::COLOR_BGR2RGB);
    } else if (mat.type() == CV_8UC4) {
        cv::cvtColor(mat, rgb, cv::COLOR_BGRA2RGB); // 丢弃 alpha
    } else if (mat.type() == CV_8UC1) {
        cv::cvtColor(mat, rgb, cv::COLOR_GRAY2RGB);
    } else {
        return slint::Image(); // 不支持的格式，返回空 Image
    }

    const int w = rgb.cols;
    const int h = rgb.rows;
    slint::SharedPixelBuffer<slint::Rgb8Pixel> buf(static_cast<uint32_t>(w), static_cast<uint32_t>(h));
    auto dst = buf.begin();

    if (rgb.isContinuous()) {
        const uint8_t *src = rgb.data;
        size_t pixels = static_cast<size_t>(w) * static_cast<size_t>(h);
        for (size_t i = 0; i < pixels; ++i) {
            // rgb 已经是 R G B 顺序
            dst[i] = slint::Rgb8Pixel{ src[i*3], src[i*3 + 1], src[i*3 + 2] };
        }
    } else {
        for (int y = 0; y < h; ++y) {
            const uint8_t *row = rgb.ptr<uint8_t>(y);
            for (int x = 0; x < w; ++x) {
                size_t i = static_cast<size_t>(y) * w + x;
                dst[i] = slint::Rgb8Pixel{ row[x*3], row[x*3 + 1], row[x*3 + 2] };
            }
        }
    }

    return slint::Image(std::move(buf));
}