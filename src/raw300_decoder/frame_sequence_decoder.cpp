#include "raw300_decoder/frame_sequence_decoder.hpp"

#include <chrono>
#include <utility>

namespace hrvtx::standalone
{

namespace
{
int64_t now_ns()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
} // namespace

FrameSequenceDecoder::FrameSequenceDecoder(const Raw300DecoderConfig &config)
    : core_(config)
{
}

bool FrameSequenceDecoder::start(std::string &err)
{
    return core_.init(err);
}

FrameSequencePacketResult
FrameSequenceDecoder::push_packet(const std::vector<uint8_t> &packet_300)
{
    FrameSequencePacketResult out;
    auto result = core_.consume_raw_packet(packet_300);

    out.packet_dropped = result.packet_dropped;
    out.waiting_for_sps_pps = result.waiting_for_sps_pps;
    out.message = result.message;
    out.counters = result.counters;

    const auto ts = now_ns();
    for (auto &mat : result.decoded_frames)
    {
        SequentialFrame frame;
        frame.frame_index = next_frame_index_++;
        frame.timestamp_ns = ts;
        frame.bgr = std::move(mat);
        ++out.frames_emitted;
        if (frame_callback_)
        {
            frame_callback_(frame);
        }
        else
        {
            frame_queue_.push_back(std::move(frame));
        }
    }
    return out;
}

bool FrameSequenceDecoder::pop_frame(SequentialFrame &out_frame)
{
    if (frame_queue_.empty())
    {
        return false;
    }
    out_frame = std::move(frame_queue_.front());
    frame_queue_.pop_front();
    return true;
}

size_t FrameSequenceDecoder::queued_frames() const
{
    return frame_queue_.size();
}

void FrameSequenceDecoder::set_frame_callback(FrameCallback cb)
{
    frame_callback_ = std::move(cb);
}

const Raw300DecoderCounters &FrameSequenceDecoder::counters() const
{
    return core_.counters();
}

} // namespace hrvtx::standalone
