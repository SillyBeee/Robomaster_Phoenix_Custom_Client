#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <vector>

#include <opencv2/core/mat.hpp>

#include "raw300_decoder/raw300_decoder.hpp"

namespace hrvtx::standalone
{

struct SequentialFrame
{
    uint64_t frame_index{0};
    int64_t timestamp_ns{0};
    cv::Mat bgr;
};

struct FrameSequencePacketResult
{
    bool packet_dropped{false};
    bool waiting_for_sps_pps{false};
    std::string message;
    size_t frames_emitted{0};
    Raw300DecoderCounters counters{};
};

class FrameSequenceDecoder
{
  public:
    using FrameCallback = std::function<void(const SequentialFrame &)>;

    explicit FrameSequenceDecoder(const Raw300DecoderConfig &config = {});

    bool start(std::string &err);

    FrameSequencePacketResult push_packet(const std::vector<uint8_t> &packet_300);

    bool pop_frame(SequentialFrame &out_frame);

    size_t queued_frames() const;

    void set_frame_callback(FrameCallback cb);

    const Raw300DecoderCounters &counters() const;

  private:
    Raw300Decoder core_;
    std::deque<SequentialFrame> frame_queue_;
    uint64_t next_frame_index_{0};
    FrameCallback frame_callback_{};
};

} // namespace hrvtx::standalone
