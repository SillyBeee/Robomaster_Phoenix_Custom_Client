#pragma once

#include <string>
#include <vector>

#include <opencv2/core/mat.hpp>

typedef struct _GstElement GstElement;

namespace hrvtx::standalone
{

class GstH264Decoder
{
  public:
    GstH264Decoder() = default;
    ~GstH264Decoder();

    bool reset(std::string &err);

    bool decode_access_unit(const std::vector<uint8_t> &access_unit,
                            std::vector<cv::Mat> &frames, std::string &err);

  private:
    void shutdown();

  private:
    GstElement *pipeline_{nullptr};
    GstElement *appsrc_{nullptr};
    GstElement *appsink_{nullptr};
};

} // namespace hrvtx::standalone
