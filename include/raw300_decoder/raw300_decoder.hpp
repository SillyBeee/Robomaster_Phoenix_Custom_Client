#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

#include <opencv2/core/mat.hpp>

#include "raw300_decoder/annexb_reassembler.hpp"
#include "raw300_decoder/gst_h264_decoder.hpp"

namespace hrvtx::standalone
{

enum class InputPacketFormat : std::uint8_t
{
    kAuto = 0,
    kRaw,
    kCompatInnerFrame,
    kCompatFragV1,
};

struct Raw300DecoderConfig
{
    size_t packet_bytes{300};
    InputPacketFormat input_packet_format{InputPacketFormat::kAuto};
    bool drop_placeholder_packets{true};
    size_t max_pending_packets{256};
    size_t max_packets_per_consume{8};
    int reset_after_decode_errors{5};
    int diagnostics_hex_len{24};
    bool diagnostics_verbose{false};
    double diagnostics_reset_log_period_sec{3.0};
    size_t reassembler_max_buffer_bytes{1 << 20};
    bool enable_decode{true};
    // 是否在收到 SPS/PPS 之前丢弃非 IDR 的 VCL。
    bool h264_wait_for_sps_pps{true};
    // 是否在收到首个 IDR 之前丢弃非 IDR 的 VCL。
    bool h264_wait_for_idr{true};
};

struct Raw300DecoderCounters
{
    uint64_t rx_packets{0};
    uint64_t dropped_packets{0};
    uint64_t raw_packets{0};
    uint64_t compat_packets{0};
    uint64_t placeholder_skipped_packets{0};
    uint64_t waiting_sps_pps_packets{0};
    uint64_t waiting_sps_pps_aus{0};
    uint64_t waiting_idr_aus{0};
    uint64_t decoded_frames{0};
    uint64_t decode_errors{0};
    uint64_t decoder_resets{0};
    uint64_t queue_overflow_drops{0};
    uint64_t transport_gap_events{0};
    uint64_t transport_gap_packets{0};
    uint64_t frame_frag_completed{0};
    uint64_t frame_frag_dropped{0};
    size_t pending_packets{0};
    ReassemblerStats reassembler{};
};

struct Raw300DecodeResult
{
    bool packet_accepted{false};
    bool packet_dropped{false};
    bool waiting_for_sps_pps{false};
    std::string message;
    std::vector<cv::Mat> decoded_frames;
    Raw300DecoderCounters counters{};
};

class Raw300Decoder
{
  public:
    explicit Raw300Decoder(const Raw300DecoderConfig &config = {});

    bool init(std::string &err);

    Raw300DecodeResult consume_raw_packet(const std::vector<uint8_t> &packet_300);
    void notify_transport_gap(uint64_t missed_packets);
    static InputPacketFormat parse_input_packet_format(const std::string &raw);
    static const char *input_packet_format_name(InputPacketFormat format);

    const Raw300DecoderCounters &counters() const;

  private:
    bool normalize_input_packet(const std::vector<uint8_t> &in,
                                std::vector<uint8_t> &out);
    bool is_placeholder_payload(const std::vector<uint8_t> &payload) const;
    bool should_decode_packet(const std::vector<uint8_t> &packet_300);
    bool should_decode_access_unit(const std::vector<int> &types);
    void on_decode_fail(const std::vector<uint8_t> &au, const std::string &err);
    void reset_decoder(const std::string &reason);
    void sync_reassembler_counters();
    Raw300DecodeResult process_one_packet(const std::vector<uint8_t> &packet_300);
    void reset_frag_assembly();

  private:
    Raw300DecoderConfig config_;
    AnnexBReassembler reassembler_;
    GstH264Decoder decoder_;
    Raw300DecoderCounters counters_{};

    bool seen_h264_sps_{false};
    bool seen_h264_pps_{false};
    bool seen_idr_{false};
    bool logged_wait_param_sets_{false};
    int consecutive_decode_errors_{0};
    int64_t last_decode_diag_log_ns_{0};
    int resets_pending_detail_{0};
    std::vector<uint8_t> last_decode_fail_au_;
    std::string last_decode_fail_exc_;
    std::deque<std::vector<uint8_t>> pending_packets_;
    int64_t last_compat_shape_warn_ns_{0};
    bool frag_active_{false};
    bool frag_broken_{false};
    std::uint16_t frag_frame_id_{0};
    std::uint8_t frag_expected_idx_{0};
    std::uint8_t frag_count_{0};
    std::vector<std::uint8_t> frag_frame_buf_;
};

} // namespace hrvtx::standalone
