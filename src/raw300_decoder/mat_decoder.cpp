#include "raw300_decoder/mat_decoder.hpp"

#include <utility>

namespace hrvtx::standalone
{

MatDecoder::MatDecoder(const Raw300DecoderConfig &config) : core_(config)
{
}

bool MatDecoder::start(std::string &err)
{
    return core_.init(err);
}

MatDecodeOutput MatDecoder::decode_packet(const std::vector<uint8_t> &packet_300)
{
    MatDecodeOutput out;
    auto result = core_.consume_raw_packet(packet_300);
    out.packet_dropped = result.packet_dropped;
    out.waiting_for_sps_pps = result.waiting_for_sps_pps;
    out.message = result.message;
    out.frames_bgr = std::move(result.decoded_frames);
    out.counters = result.counters;
    return out;
}

void MatDecoder::notify_transport_gap(uint64_t missed_packets)
{
    core_.notify_transport_gap(missed_packets);
}

const Raw300DecoderCounters &MatDecoder::counters() const
{
    return core_.counters();
}

} // namespace hrvtx::standalone
