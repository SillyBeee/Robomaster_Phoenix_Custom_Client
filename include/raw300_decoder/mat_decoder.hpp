#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <opencv2/core/mat.hpp>

#include "raw300_decoder/raw300_decoder.hpp"

namespace hrvtx::standalone
{

struct MatDecodeOutput
{
    bool packet_dropped{false};
    bool waiting_for_sps_pps{false};
    std::string message;
    std::vector<cv::Mat> frames_bgr;
    Raw300DecoderCounters counters{};
};

class MatDecoder
{
  public:
    explicit MatDecoder(const Raw300DecoderConfig &config = {});

    bool start(std::string &err);

    MatDecodeOutput decode_packet(const std::vector<uint8_t> &packet_300);
    void notify_transport_gap(uint64_t missed_packets);

    const Raw300DecoderCounters &counters() const;

  private:
    Raw300Decoder core_;
};

} // namespace hrvtx::standalone
