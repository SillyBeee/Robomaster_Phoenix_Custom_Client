#include "raw300_decoder/raw300_decoder.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <utility>

#include "raw300_decoder/decoder_utils.hpp"

namespace hrvtx::standalone
{

namespace
{
constexpr std::array<std::uint8_t, 5> kPlaceholderMagic{
    0x4C, 0x55, 0x43, 0x4B, 0x59}; // "LUCKY"
constexpr std::uint8_t kCompatFrameStart = static_cast<std::uint8_t>('v');
constexpr std::uint8_t kCompatFrameEnd = static_cast<std::uint8_t>('x');
constexpr std::uint8_t kCompatFragMagic = static_cast<std::uint8_t>('F');
constexpr std::uint8_t kCompatFragVersion = 0x01;
constexpr std::size_t kCompatFragHeaderBytes = 10U;
constexpr std::size_t kCompatLegacyHeaderBytes = 3U;

int64_t now_ns()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
} // namespace

Raw300Decoder::Raw300Decoder(const Raw300DecoderConfig &config)
    : config_(config), reassembler_(config.reassembler_max_buffer_bytes)
{
}

bool Raw300Decoder::init(std::string &err)
{
    return decoder_.reset(err);
}

Raw300DecodeResult
Raw300Decoder::consume_raw_packet(const std::vector<uint8_t> &packet_300)
{
    Raw300DecodeResult out;

    if (packet_300.size() != config_.packet_bytes)
    {
        ++counters_.dropped_packets;
        out.packet_dropped = true;
        out.message = "input packet size mismatch";
        out.counters = counters_;
        return out;
    }
    ++counters_.rx_packets;

    std::vector<uint8_t> normalized;
    if (!normalize_input_packet(packet_300, normalized))
    {
        ++counters_.dropped_packets;
        out.packet_dropped = true;
        out.message = "invalid input packet format";
        out.counters = counters_;
        return out;
    }
    if (config_.drop_placeholder_packets && is_placeholder_payload(normalized))
    {
        ++counters_.placeholder_skipped_packets;
        out.packet_accepted = true;
        out.message = "placeholder packet skipped";
        out.counters = counters_;
        return out;
    }

    if (normalized.empty())
    {
        out.packet_accepted = true;
        out.message = "waiting complete frame fragments";
        out.counters = counters_;
        return out;
    }

    out.packet_accepted = true;
    pending_packets_.push_back(std::move(normalized));
    if (pending_packets_.size() > config_.max_pending_packets)
    {
        pending_packets_.pop_front();
        ++counters_.queue_overflow_drops;
        ++counters_.dropped_packets;
        out.message = "decoder queue overflow, drop oldest packet";
    }
    counters_.pending_packets = pending_packets_.size();

    const auto to_process =
        std::min(pending_packets_.size(), config_.max_packets_per_consume);
    for (size_t i = 0; i < to_process; ++i)
    {
        auto one = process_one_packet(pending_packets_.front());
        pending_packets_.pop_front();
        if (one.waiting_for_sps_pps)
        {
            out.waiting_for_sps_pps = true;
        }
        if (one.packet_dropped)
        {
            out.packet_dropped = true;
        }
        if (!one.message.empty())
        {
            out.message = one.message;
        }
        out.decoded_frames.insert(out.decoded_frames.end(),
                                  one.decoded_frames.begin(),
                                  one.decoded_frames.end());
    }
    counters_.pending_packets = pending_packets_.size();

    out.counters = counters_;
    return out;
}

void Raw300Decoder::notify_transport_gap(uint64_t missed_packets)
{
    if (missed_packets == 0)
    {
        return;
    }
    ++counters_.transport_gap_events;
    counters_.transport_gap_packets += missed_packets;

    // 关键：一旦链路缺包，清空重组与解码状态，等待下一次 IDR，
    // 避免继续使用损坏参考链导致持续花屏/糊屏。
    pending_packets_.clear();
    counters_.pending_packets = 0;
    reset_frag_assembly();
    reassembler_.flush();
    reset_decoder("transport gap");
}

const Raw300DecoderCounters &Raw300Decoder::counters() const
{
    return counters_;
}

void Raw300Decoder::reset_frag_assembly()
{
    frag_active_ = false;
    frag_broken_ = false;
    frag_frame_id_ = 0;
    frag_expected_idx_ = 0;
    frag_count_ = 0;
    frag_frame_buf_.clear();
}

InputPacketFormat Raw300Decoder::parse_input_packet_format(const std::string &raw)
{
    std::string s = raw;
    std::transform(
        s.begin(), s.end(), s.begin(),
        [](unsigned char c)
        { return static_cast<char>(std::tolower(c)); });
    if (s == "raw")
    {
        return InputPacketFormat::kRaw;
    }
    if (s == "compat_inner_frame" || s == "compat")
    {
        return InputPacketFormat::kCompatInnerFrame;
    }
    if (s == "compat_frag_v1" || s == "compat_frame_frag_v1" ||
        s == "compat_v1")
    {
        return InputPacketFormat::kCompatFragV1;
    }
    return InputPacketFormat::kAuto;
}

const char *Raw300Decoder::input_packet_format_name(InputPacketFormat format)
{
    switch (format)
    {
    case InputPacketFormat::kRaw:
        return "raw";
    case InputPacketFormat::kCompatInnerFrame:
        return "compat_inner_frame";
    case InputPacketFormat::kCompatFragV1:
        return "compat_frag_v1";
    default:
        return "auto";
    }
}

bool Raw300Decoder::normalize_input_packet(const std::vector<uint8_t> &in,
                                           std::vector<uint8_t> &out)
{
    if (in.empty())
    {
        return false;
    }
    const bool compat_candidate =
        in.size() == config_.packet_bytes && in.size() >= 4U &&
        in.front() == kCompatFrameStart && in.back() == kCompatFrameEnd;
    const bool compat_frag_candidate =
        compat_candidate && in.size() > kCompatFragHeaderBytes &&
        in[3] == kCompatFragMagic && in[4] == kCompatFragVersion;

    const bool decode_as_compat_frag =
        config_.input_packet_format == InputPacketFormat::kCompatFragV1 ||
        (config_.input_packet_format == InputPacketFormat::kAuto &&
         compat_frag_candidate);
    if (decode_as_compat_frag)
    {
        if (!compat_frag_candidate)
        {
            if (config_.input_packet_format == InputPacketFormat::kCompatFragV1)
            {
                if (now_ns() - last_compat_shape_warn_ns_ >=
                    static_cast<int64_t>(5e9))
                {
                    last_compat_shape_warn_ns_ = now_ns();
                }
                return false;
            }
        }
        else
        {
            ++counters_.compat_packets;
            const std::uint16_t frame_id = static_cast<std::uint16_t>(in[5]) |
                                           (static_cast<std::uint16_t>(in[6]) << 8);
            const std::uint8_t frag_idx = in[7];
            const std::uint8_t frag_cnt = in[8];

            if (frag_cnt == 0U || frag_idx >= frag_cnt)
            {
                ++counters_.frame_frag_dropped;
                reset_frag_assembly();
                out.clear();
                return true;
            }

            const bool frame_changed = !frag_active_ || frame_id != frag_frame_id_;
            if (frame_changed)
            {
                if (frag_active_ && frag_expected_idx_ != frag_count_)
                {
                    ++counters_.frame_frag_dropped;
                }
                frag_active_ = true;
                frag_broken_ = false;
                frag_frame_id_ = frame_id;
                frag_expected_idx_ = 0;
                frag_count_ = frag_cnt;
                frag_frame_buf_.clear();
            }

            if (frag_cnt != frag_count_ || frag_idx != frag_expected_idx_)
            {
                frag_broken_ = true;
            }

            if (!frag_broken_)
            {
                frag_frame_buf_.insert(
                    frag_frame_buf_.end(), in.begin() + kCompatFragHeaderBytes,
                    in.end() - 1);
            }

            frag_expected_idx_ = static_cast<std::uint8_t>(frag_idx + 1U);
            if (frag_idx + 1U >= frag_cnt)
            {
                if (!frag_broken_ && !frag_frame_buf_.empty())
                {
                    out = std::move(frag_frame_buf_);
                    ++counters_.frame_frag_completed;
                }
                else
                {
                    ++counters_.frame_frag_dropped;
                    out.clear();
                }
                reset_frag_assembly();
            }
            else
            {
                out.clear();
            }
            return true;
        }
    }

    const bool decode_as_compat =
        config_.input_packet_format == InputPacketFormat::kCompatInnerFrame ||
        (config_.input_packet_format == InputPacketFormat::kAuto &&
         compat_candidate);
    if (decode_as_compat)
    {
        if (!compat_candidate)
        {
            if (config_.input_packet_format == InputPacketFormat::kCompatInnerFrame)
            {
                if (now_ns() - last_compat_shape_warn_ns_ >=
                    static_cast<int64_t>(5e9))
                {
                    last_compat_shape_warn_ns_ = now_ns();
                }
                return false;
            }
        }
        else
        {
            ++counters_.compat_packets;
            out.assign(in.begin() + kCompatLegacyHeaderBytes, in.end() - 1);
            return true;
        }
    }
    reset_frag_assembly();
    ++counters_.raw_packets;
    out = in;
    return true;
}

bool Raw300Decoder::is_placeholder_payload(const std::vector<uint8_t> &payload) const
{
    if (payload.size() < kPlaceholderMagic.size())
    {
        return false;
    }
    if (!std::equal(kPlaceholderMagic.begin(), kPlaceholderMagic.end(),
                    payload.begin()))
    {
        return false;
    }
    for (std::size_t i = kPlaceholderMagic.size(); i < payload.size(); ++i)
    {
        if (payload[i] != 0U)
        {
            return false;
        }
    }
    return true;
}

bool Raw300Decoder::should_decode_packet(const std::vector<uint8_t> &packet_300)
{
    const auto types = nal_unit_types(packet_300);
    if (contains_type(types, 7))
    {
        seen_h264_sps_ = true;
    }
    if (contains_type(types, 8))
    {
        seen_h264_pps_ = true;
    }
    if (types.empty())
    {
        return true;
    }
    if (contains_type(types, 5) || contains_type(types, 7) ||
        contains_type(types, 8))
    {
        return true;
    }
    if (contains_type(types, 1))
    {
        if (!config_.h264_wait_for_sps_pps ||
            (seen_h264_sps_ && seen_h264_pps_))
        {
            return true;
        }
        if (!logged_wait_param_sets_)
        {
            logged_wait_param_sets_ = true;
        }
        return false;
    }
    return true;
}

bool Raw300Decoder::should_decode_access_unit(const std::vector<int> &types)
{
    if (types.empty())
    {
        return false;
    }

    const bool has_idr = contains_type(types, 5);
    const bool has_non_idr = contains_type(types, 1);
    const bool has_vcl = has_idr || has_non_idr;
    if (!has_vcl)
    {
        return false;
    }

    if (config_.h264_wait_for_sps_pps && !has_idr &&
        !(seen_h264_sps_ && seen_h264_pps_))
    {
        ++counters_.waiting_sps_pps_aus;
        return false;
    }
    if (config_.h264_wait_for_idr && !has_idr && !seen_idr_)
    {
        ++counters_.waiting_idr_aus;
        return false;
    }
    return true;
}

void Raw300Decoder::on_decode_fail(const std::vector<uint8_t> &au,
                                   const std::string &err)
{
    ++consecutive_decode_errors_;
    ++counters_.decode_errors;
    last_decode_fail_au_ = au;
    last_decode_fail_exc_ = err;

    if (config_.reset_after_decode_errors > 0 &&
        consecutive_decode_errors_ >= config_.reset_after_decode_errors)
    {
        ++resets_pending_detail_;
        const auto period_ns =
            static_cast<int64_t>(config_.diagnostics_reset_log_period_sec * 1e9);
        if (now_ns() - last_decode_diag_log_ns_ >= period_ns)
        {
            last_decode_diag_log_ns_ = now_ns();
            resets_pending_detail_ = 0;
        }
        reset_decoder("decode errors");
    }
}

void Raw300Decoder::reset_decoder(const std::string &reason)
{
    (void)reason;
    std::string err;
    if (!decoder_.reset(err))
    {
        return;
    }
    ++counters_.decoder_resets;
    consecutive_decode_errors_ = 0;
    seen_h264_sps_ = false;
    seen_h264_pps_ = false;
    seen_idr_ = false;
    reset_frag_assembly();
}

void Raw300Decoder::sync_reassembler_counters()
{
    counters_.reassembler = reassembler_.stats();
}

Raw300DecodeResult
Raw300Decoder::process_one_packet(const std::vector<uint8_t> &packet_300)
{
    Raw300DecodeResult out;
    if (!should_decode_packet(packet_300))
    {
        ++counters_.waiting_sps_pps_packets;
        out.waiting_for_sps_pps = true;
        out.message = "waiting for SPS/PPS before decoding P-frames";
        return out;
    }

    reassembler_.push_chunk(packet_300);
    sync_reassembler_counters();

    auto access_units = reassembler_.take_ready_access_units();
    if (!config_.enable_decode)
    {
        for (const auto &au : access_units)
        {
            const auto types = nal_unit_types(au);
            if (contains_type(types, 7))
            {
                seen_h264_sps_ = true;
            }
            if (contains_type(types, 8))
            {
                seen_h264_pps_ = true;
            }
            if (contains_type(types, 5))
            {
                seen_idr_ = true;
            }
            if (!should_decode_access_unit(types))
            {
                continue;
            }
        }
        return out;
    }
    for (const auto &au : access_units)
    {
        const auto types = nal_unit_types(au);
        if (contains_type(types, 7))
        {
            seen_h264_sps_ = true;
        }
        if (contains_type(types, 8))
        {
            seen_h264_pps_ = true;
        }
        if (contains_type(types, 5))
        {
            seen_idr_ = true;
        }

        if (!should_decode_access_unit(types))
        {
            continue;
        }

        std::vector<cv::Mat> frames;
        std::string err;
        if (!decoder_.decode_access_unit(au, frames, err))
        {
            on_decode_fail(au, err);
            out.message = err;
            continue;
        }

        consecutive_decode_errors_ = 0;
        counters_.decoded_frames += static_cast<uint64_t>(frames.size());
        out.decoded_frames.insert(out.decoded_frames.end(), frames.begin(),
                                  frames.end());
    }
    return out;
}

} // namespace hrvtx::standalone
