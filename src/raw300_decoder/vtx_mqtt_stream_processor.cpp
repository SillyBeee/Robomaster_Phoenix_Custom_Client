#include "raw300_decoder/vtx_mqtt_stream_processor.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <limits>
#include <sstream>
#include <utility>

#include <opencv2/videoio.hpp>

#include "logger.hpp"

namespace hrvtx::standalone
{

namespace
{
constexpr size_t kMaxQueuedVideoFrames = 300;
constexpr double kVideoOutputFps = 30.0;
constexpr double kWarmupDurationS = 2.0;
constexpr int64_t kReassemblyWarnPeriodNs = static_cast<int64_t>(2e9);
constexpr uint64_t kTransportGapResetThresholdPackets = 2;

uint32_t read_u32_le(const std::vector<uint8_t> &data, size_t off)
{
    return static_cast<uint32_t>(data[off]) |
           (static_cast<uint32_t>(data[off + 1]) << 8) |
           (static_cast<uint32_t>(data[off + 2]) << 16) |
           (static_cast<uint32_t>(data[off + 3]) << 24);
}

const char *seq_format_name(VtxMqttStreamProcessor::SeqFormat format)
{
    switch (format)
    {
    case VtxMqttStreamProcessor::SeqFormat::kLegacy16At1:
        return "legacy16@1";
    case VtxMqttStreamProcessor::SeqFormat::kTest32At6:
        return "test32@6";
    default:
        return "unknown";
    }
}

double percentile_ms(std::vector<uint64_t> samples_ns, double q)
{
    if (samples_ns.empty())
    {
        return 0.0;
    }
    if (samples_ns.size() == 1)
    {
        return static_cast<double>(samples_ns.front()) / 1e6;
    }
    std::sort(samples_ns.begin(), samples_ns.end());
    const double rank = q * static_cast<double>(samples_ns.size() - 1);
    const auto idx = static_cast<size_t>(std::lround(rank));
    return static_cast<double>(samples_ns[idx]) / 1e6;
}
} // namespace

VtxMqttStreamProcessor::VtxMqttStreamProcessor(const std::filesystem::path &output_dir,
                                               std::string test_mode_config,
                                               bool h264_wait_for_sps_pps,
                                               bool h264_wait_for_idr)
    : output_dir_(output_dir),
      video_path_(output_dir_ / "mqtt_stream_realtime.avi"),
      packet_dump_path_(output_dir_ / "mqtt_packets_300b.txt"),
      non_zero_stats_path_(output_dir_ / "mqtt_non_zero_stats.txt"),
      abnormal_dump_path_(output_dir_ / "mqtt_abnormal_packets.txt"),
      loss_stats_path_(output_dir_ / "mqtt_packet_loss_stats.txt"),
      decode_bench_path_(output_dir_ / "mqtt_decode_benchmark_stats.txt"),
      stats_window_begin_(std::chrono::steady_clock::now()),
      stats_window_min_non_zero_(std::numeric_limits<size_t>::max())
{
    warmup_duration_s_ = kWarmupDurationS;
    test_mode_config_ = std::move(test_mode_config);
    h264_wait_for_sps_pps_ = h264_wait_for_sps_pps;
    h264_wait_for_idr_ = h264_wait_for_idr;
}

VtxMqttStreamProcessor::~VtxMqttStreamProcessor()
{
    stop();
}

bool VtxMqttStreamProcessor::start(std::string &err)
{
    if (running_.load())
    {
        return true;
    }

    std::filesystem::create_directories(output_dir_);

    packet_dump_file_.open(packet_dump_path_, std::ios::out | std::ios::trunc);
    non_zero_stats_file_.open(non_zero_stats_path_, std::ios::out | std::ios::trunc);
    abnormal_dump_file_.open(abnormal_dump_path_, std::ios::out | std::ios::trunc);
    loss_stats_file_.open(loss_stats_path_, std::ios::out | std::ios::trunc);
    decode_bench_file_.open(decode_bench_path_, std::ios::out | std::ios::trunc);

    if (!packet_dump_file_.is_open())
    {
        LOG_ERROR("Failed to open mqtt packet dump file: {}", packet_dump_path_.string());
    }
    else
    {
        LOG_INFO("MQTT packet dump enabled: {}", packet_dump_path_.string());
    }

    if (!non_zero_stats_file_.is_open())
    {
        LOG_ERROR("Failed to open mqtt non-zero stats file: {}", non_zero_stats_path_.string());
    }
    else
    {
        LOG_INFO("MQTT non-zero stats file enabled: {}", non_zero_stats_path_.string());
    }

    if (!abnormal_dump_file_.is_open())
    {
        LOG_ERROR("Failed to open mqtt abnormal packet file: {}", abnormal_dump_path_.string());
    }
    else
    {
        LOG_INFO("MQTT abnormal packet file enabled: {}", abnormal_dump_path_.string());
    }

    if (!loss_stats_file_.is_open())
    {
        LOG_ERROR("Failed to open mqtt packet loss stats file: {}", loss_stats_path_.string());
    }
    else
    {
        LOG_INFO("MQTT packet loss stats file enabled: {}", loss_stats_path_.string());
    }

    if (!decode_bench_file_.is_open())
    {
        LOG_ERROR("Failed to open mqtt decode benchmark file: {}", decode_bench_path_.string());
    }
    else
    {
        LOG_INFO("MQTT decode benchmark file enabled: {}", decode_bench_path_.string());
    }

    TestMode parsed_mode = TestMode::kFullPipeline;
    if (TryParseTestMode(test_mode_config_, parsed_mode))
    {
        test_mode_ = parsed_mode;
    }
    else
    {
        test_mode_ = ParseTestModeFromEnv();
    }
    LOG_INFO("MQTT stream test mode: {}", TestModeName(test_mode_));
    LOG_INFO("Raw300 decoder compatibility: wait_sps_pps={}, wait_idr={}",
             h264_wait_for_sps_pps_ ? "true" : "false",
             h264_wait_for_idr_ ? "true" : "false");
    if (test_mode_ != TestMode::kReceiveOnly)
    {
        Raw300DecoderConfig cfg{};
        cfg.enable_decode = (test_mode_ == TestMode::kFullPipeline);
        cfg.h264_wait_for_sps_pps = h264_wait_for_sps_pps_;
        cfg.h264_wait_for_idr = h264_wait_for_idr_;
        decoder_ = std::make_unique<MatDecoder>(cfg);
        if (!decoder_->start(err))
        {
            decoder_.reset();
            return false;
        }
    }
    else
    {
        decoder_.reset();
    }

    video_writer_stop_.store(false);
    video_drop_count_.store(0);
    {
        std::lock_guard<std::mutex> lock(io_mutex_);
        warmup_active_ = warmup_duration_s_ > 0.0;
        warmup_end_ = std::chrono::steady_clock::now() +
                      std::chrono::milliseconds(
                          static_cast<int64_t>(warmup_duration_s_ * 1000.0));
        warmup_packets_ = 0;
        warmup_missing_ = 0;
        warmup_dropped_ = 0;
        warmup_reassembly_resync_ = 0;
        warmup_reassembly_au_dropped_ = 0;
        reassembly_last_resync_ = 0;
        reassembly_last_au_dropped_ = 0;
        reassembly_window_resync_ = 0;
        reassembly_window_au_dropped_ = 0;
        reassembly_total_resync_ = 0;
        reassembly_total_au_dropped_ = 0;
        decode_window_packets_ = 0;
        decode_window_frames_ = 0;
        decode_window_ns_ = 0;
        decode_window_max_ns_ = 0;
        decode_total_packets_ = 0;
        decode_total_frames_ = 0;
        decode_total_ns_ = 0;
        decode_total_min_ns_ = 0;
        decode_total_max_ns_ = 0;
        decode_window_samples_ns_.clear();
        decode_window_samples_ns_.reserve(128);
        seq_format_ = SeqFormat::kUnknown;
        has_last_seq16_ = false;
        has_last_seq32_ = false;
        last_seq16_ = 0;
        last_seq32_ = 0;
        last_reassembly_warn_ = std::chrono::steady_clock::time_point{};
        if (warmup_active_)
        {
            LOG_INFO("Decoder warmup enabled: {:.1f}s", warmup_duration_s_);
        }
    }
    video_writer_thread_ = std::thread(&VtxMqttStreamProcessor::VideoWriterLoop, this);
    running_.store(true);
    return true;
}

void VtxMqttStreamProcessor::stop()
{
    if (!running_.exchange(false))
    {
        return;
    }

    video_writer_stop_.store(true);
    video_queue_cv_.notify_all();
    if (video_writer_thread_.joinable())
    {
        video_writer_thread_.join();
    }

    std::lock_guard<std::mutex> lock(io_mutex_);
    packet_dump_file_.close();
    non_zero_stats_file_.close();
    abnormal_dump_file_.close();
    loss_stats_file_.close();
    decode_bench_file_.close();
}

void VtxMqttStreamProcessor::SetFrameCallback(std::function<void(cv::Mat)> cb)
{
    frame_callback_ = std::move(cb);
}

void VtxMqttStreamProcessor::OnPacket(const std::vector<uint8_t> &packet_data)
{
    if (!running_.load())
    {
        return;
    }

    const size_t non_zero_bytes = static_cast<size_t>(
        std::count_if(packet_data.begin(), packet_data.end(), [](uint8_t b)
                      { return b != 0; }));

    const auto now = std::chrono::steady_clock::now();
    uint64_t transport_gap_missed = 0;
    bool in_warmup = false;
    {
        std::lock_guard<std::mutex> lock(io_mutex_);
        if (warmup_active_ && now >= warmup_end_)
        {
            warmup_active_ = false;
            LOG_INFO(
                "Decoder warmup finished: packets={}, missing_seq={}, dropped={}, "
                "reassembly_resync={}, reassembly_au_dropped={}",
                warmup_packets_, warmup_missing_, warmup_dropped_,
                warmup_reassembly_resync_, warmup_reassembly_au_dropped_);
        }
        in_warmup = warmup_active_;
        if (in_warmup)
        {
            ++warmup_packets_;
        }
        ++stats_window_packets_;
        stats_window_non_zero_bytes_ += non_zero_bytes;
        stats_window_min_non_zero_ = std::min(stats_window_min_non_zero_, non_zero_bytes);
        stats_window_max_non_zero_ = std::max(stats_window_max_non_zero_, non_zero_bytes);

        if (packet_data.size() >= 3)
        {
            SeqFormat this_format = SeqFormat::kLegacy16At1;
            if (packet_data.size() >= 10 && packet_data[0] == 0x54 &&
                packet_data[1] == 0x47 && packet_data[2] == 0x01)
            {
                this_format = SeqFormat::kTest32At6;
            }

            if (!in_warmup)
            {
                ++loss_window_received_;
                ++loss_total_received_;
            }

            if (seq_format_ != SeqFormat::kUnknown && seq_format_ != this_format)
            {
                // Sequence field layout changed mid-stream, treat as resync once.
                if (!in_warmup)
                {
                    ++loss_total_resync_;
                    transport_gap_missed += 1;
                }
                has_last_seq16_ = false;
                has_last_seq32_ = false;
            }
            seq_format_ = this_format;

            if (this_format == SeqFormat::kTest32At6)
            {
                const uint32_t seq = read_u32_le(packet_data, 6);
                if (has_last_seq32_)
                {
                    const uint32_t step = seq - last_seq32_;
                    if (step == 0U)
                    {
                        if (!in_warmup)
                        {
                            ++loss_total_duplicate_;
                        }
                    }
                    else if (step > 0x7fffffffU)
                    {
                        if (!in_warmup)
                        {
                            ++loss_total_resync_;
                        }
                    }
                    else if (step > 1U)
                    {
                        const uint64_t missed = static_cast<uint64_t>(step - 1U);
                        if (in_warmup)
                        {
                            warmup_missing_ += missed;
                        }
                        else
                        {
                            loss_window_missing_ += missed;
                            loss_total_missing_ += missed;
                            transport_gap_missed += missed;
                        }
                    }
                }
                last_seq32_ = seq;
                has_last_seq32_ = true;
            }
            else
            {
                const uint16_t seq =
                    static_cast<uint16_t>(packet_data[1]) |
                    (static_cast<uint16_t>(packet_data[2]) << 8);
                if (has_last_seq16_)
                {
                    const uint16_t step = static_cast<uint16_t>(seq - last_seq16_);
                    if (step == 0)
                    {
                        if (!in_warmup)
                        {
                            ++loss_total_duplicate_;
                        }
                    }
                    else if (step > 0x7fff)
                    {
                        if (!in_warmup)
                        {
                            ++loss_total_resync_;
                        }
                    }
                    else if (step > 1)
                    {
                        const uint64_t missed = static_cast<uint64_t>(step - 1);
                        if (in_warmup)
                        {
                            warmup_missing_ += missed;
                        }
                        else
                        {
                            loss_window_missing_ += missed;
                            loss_total_missing_ += missed;
                            transport_gap_missed += missed;
                        }
                    }
                }
                last_seq16_ = seq;
                has_last_seq16_ = true;
            }
        }

        if (packet_dump_file_.is_open())
        {
            packet_dump_file_ << PacketToHexLine(packet_data) << '\n';
            packet_dump_file_.flush();
        }

        if (now - stats_window_begin_ >= std::chrono::seconds(1))
        {
            WriteWindowStatsLocked(now);
        }
    }

    if (test_mode_ == TestMode::kReceiveOnly || !decoder_)
    {
        return;
    }

    if (transport_gap_missed >= kTransportGapResetThresholdPackets)
    {
        decoder_->notify_transport_gap(transport_gap_missed);
    }

    const auto decode_begin = std::chrono::steady_clock::now();
    auto out = decoder_->decode_packet(packet_data);
    const auto decode_elapsed_ns = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - decode_begin)
            .count());
    {
        std::lock_guard<std::mutex> lock(io_mutex_);
        if (warmup_active_ && now >= warmup_end_)
        {
            warmup_active_ = false;
            LOG_INFO(
                "Decoder warmup finished: packets={}, missing_seq={}, dropped={}, "
                "reassembly_resync={}, reassembly_au_dropped={}",
                warmup_packets_, warmup_missing_, warmup_dropped_,
                warmup_reassembly_resync_, warmup_reassembly_au_dropped_);
        }
        const bool warmup_now = warmup_active_;
        ++decode_window_packets_;
        decode_window_frames_ += static_cast<uint64_t>(out.frames_bgr.size());
        decode_window_ns_ += decode_elapsed_ns;
        decode_window_max_ns_ = std::max(decode_window_max_ns_, decode_elapsed_ns);
        decode_window_samples_ns_.push_back(decode_elapsed_ns);
        ++decode_total_packets_;
        decode_total_frames_ += static_cast<uint64_t>(out.frames_bgr.size());
        decode_total_ns_ += decode_elapsed_ns;
        if (decode_total_min_ns_ == 0)
        {
            decode_total_min_ns_ = decode_elapsed_ns;
        }
        else
        {
            decode_total_min_ns_ = std::min(decode_total_min_ns_, decode_elapsed_ns);
        }
        decode_total_max_ns_ = std::max(decode_total_max_ns_, decode_elapsed_ns);
        const uint64_t resync_now = out.counters.reassembler.resync_count;
        const uint64_t au_drop_now = out.counters.reassembler.au_dropped;
        uint64_t resync_delta = 0;
        uint64_t au_drop_delta = 0;
        if (resync_now >= reassembly_last_resync_)
        {
            resync_delta = resync_now - reassembly_last_resync_;
        }
        if (au_drop_now >= reassembly_last_au_dropped_)
        {
            au_drop_delta = au_drop_now - reassembly_last_au_dropped_;
        }
        reassembly_last_resync_ = resync_now;
        reassembly_last_au_dropped_ = au_drop_now;
        reassembly_window_resync_ += resync_delta;
        reassembly_window_au_dropped_ += au_drop_delta;
        reassembly_total_resync_ += resync_delta;
        reassembly_total_au_dropped_ += au_drop_delta;
        if (warmup_now)
        {
            warmup_reassembly_resync_ += resync_delta;
            warmup_reassembly_au_dropped_ += au_drop_delta;
        }

        if (resync_delta > 0 || au_drop_delta > 0)
        {
            if (abnormal_dump_file_.is_open())
            {
                abnormal_dump_file_
                    << "reason=reassembly_issue"
                    << ", warmup=" << (warmup_now ? 1 : 0)
                    << ", resync_delta=" << resync_delta
                    << ", au_dropped_delta=" << au_drop_delta
                    << ", size=" << packet_data.size()
                    << ", data=" << PacketToHexLine(packet_data) << '\n';
                abnormal_dump_file_.flush();
            }
            const auto now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                    now.time_since_epoch())
                                    .count();
            const auto last_warn_ns =
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    last_reassembly_warn_.time_since_epoch())
                    .count();
            if (last_reassembly_warn_.time_since_epoch().count() == 0 ||
                now_ns - last_warn_ns >= kReassemblyWarnPeriodNs)
            {
                last_reassembly_warn_ = now;
                LOG_WARN(
                    "Reassembly issue detected (warmup={}): resync_delta={}, "
                    "au_dropped_delta={}",
                    warmup_now ? 1 : 0, resync_delta, au_drop_delta);
            }
        }

        if (out.packet_dropped)
        {
            if (warmup_now)
            {
                ++warmup_dropped_;
            }
            if (abnormal_dump_file_.is_open())
            {
                abnormal_dump_file_
                    << "reason=" << out.message
                    << ", warmup=" << (warmup_now ? 1 : 0)
                    << ", size=" << packet_data.size()
                    << ", data=" << PacketToHexLine(packet_data) << '\n';
                abnormal_dump_file_.flush();
            }
            LOG_WARN("Drop raw300 packet (warmup={}): {}", warmup_now ? 1 : 0,
                     out.message);
            return;
        }
    }

    for (const auto &frame : out.frames_bgr)
    {
        if (frame.empty())
        {
            continue;
        }
        TimedFrame item;
        item.frame_bgr = frame.clone();
        item.timestamp = std::chrono::steady_clock::now();
        if (frame_callback_) {
            frame_callback_(item.frame_bgr);
        }
        std::lock_guard<std::mutex> lock(video_queue_mutex_);
        if (video_queue_.size() >= kMaxQueuedVideoFrames)
        {
            video_queue_.pop_front();
            video_drop_count_.fetch_add(1);
        }
        video_queue_.push_back(std::move(item));
        video_queue_cv_.notify_one();
    }
}

std::string VtxMqttStreamProcessor::PacketToHexLine(const std::vector<uint8_t> &packet_data)
{
    std::ostringstream line;
    line << std::hex << std::setfill('0');
    for (size_t i = 0; i < packet_data.size(); ++i)
    {
        if (i > 0)
        {
            line << ' ';
        }
        line << std::setw(2) << static_cast<unsigned int>(packet_data[i]);
    }
    return line.str();
}

void VtxMqttStreamProcessor::WriteWindowStatsLocked(const std::chrono::steady_clock::time_point &now)
{
    const double avg_non_zero =
        (stats_window_packets_ > 0)
            ? static_cast<double>(stats_window_non_zero_bytes_) / static_cast<double>(stats_window_packets_)
            : 0.0;
    if (non_zero_stats_file_.is_open())
    {
        non_zero_stats_file_
            << "pkts=" << stats_window_packets_
            << ", avg_non_zero_bytes=" << std::fixed << std::setprecision(1) << avg_non_zero
            << ", min=" << stats_window_min_non_zero_
            << ", max=" << stats_window_max_non_zero_ << '\n';
        non_zero_stats_file_.flush();
    }

    if (loss_stats_file_.is_open())
    {
        const uint64_t window_expected = loss_window_received_ + loss_window_missing_;
        const double window_loss_rate =
            (window_expected > 0)
                ? (100.0 * static_cast<double>(loss_window_missing_) / static_cast<double>(window_expected))
                : 0.0;
        const uint64_t total_expected = loss_total_received_ + loss_total_missing_;
        const double total_loss_rate =
            (total_expected > 0)
                ? (100.0 * static_cast<double>(loss_total_missing_) / static_cast<double>(total_expected))
                : 0.0;
        loss_stats_file_
            << "window_recv=" << loss_window_received_
            << ", window_missing=" << loss_window_missing_
            << ", window_loss_rate=" << std::fixed << std::setprecision(2) << window_loss_rate << "%"
            << ", total_recv=" << loss_total_received_
            << ", total_missing=" << loss_total_missing_
            << ", total_loss_rate=" << std::fixed << std::setprecision(2) << total_loss_rate << "%"
            << ", total_dup=" << loss_total_duplicate_
            << ", total_resync=" << loss_total_resync_
            << ", reassembly_window_resync=" << reassembly_window_resync_
            << ", reassembly_window_au_dropped=" << reassembly_window_au_dropped_
            << ", reassembly_total_resync=" << reassembly_total_resync_
            << ", reassembly_total_au_dropped=" << reassembly_total_au_dropped_
            << ", seq_format=" << seq_format_name(seq_format_)
            << ", test_mode=" << TestModeName(test_mode_)
            << ", warmup_active=" << (warmup_active_ ? 1 : 0) << '\n';
        loss_stats_file_.flush();
    }

    if (decode_bench_file_.is_open())
    {
        const double window_avg_ms = (decode_window_packets_ > 0)
                                         ? (static_cast<double>(decode_window_ns_) /
                                            static_cast<double>(decode_window_packets_) / 1e6)
                                         : 0.0;
        const double window_min_ms = decode_window_samples_ns_.empty()
                                         ? 0.0
                                         : (static_cast<double>(
                                                *std::min_element(decode_window_samples_ns_.begin(),
                                                                  decode_window_samples_ns_.end())) /
                                            1e6);
        const double window_max_ms = static_cast<double>(decode_window_max_ns_) / 1e6;
        const double window_p50_ms = percentile_ms(decode_window_samples_ns_, 0.50);
        const double window_p95_ms = percentile_ms(decode_window_samples_ns_, 0.95);
        const double total_avg_ms = (decode_total_packets_ > 0)
                                        ? (static_cast<double>(decode_total_ns_) /
                                           static_cast<double>(decode_total_packets_) / 1e6)
                                        : 0.0;
        const double total_min_ms = static_cast<double>(decode_total_min_ns_) / 1e6;
        const double total_max_ms = static_cast<double>(decode_total_max_ns_) / 1e6;
        decode_bench_file_
            << "mode=" << TestModeName(test_mode_)
            << ", window_packets=" << decode_window_packets_
            << ", window_frames=" << decode_window_frames_
            << ", window_decode_avg_ms=" << std::fixed << std::setprecision(3) << window_avg_ms
            << ", window_decode_min_ms=" << std::fixed << std::setprecision(3) << window_min_ms
            << ", window_decode_max_ms=" << std::fixed << std::setprecision(3) << window_max_ms
            << ", window_decode_p50_ms=" << std::fixed << std::setprecision(3) << window_p50_ms
            << ", window_decode_p95_ms=" << std::fixed << std::setprecision(3) << window_p95_ms
            << ", total_packets=" << decode_total_packets_
            << ", total_frames=" << decode_total_frames_
            << ", total_decode_avg_ms=" << std::fixed << std::setprecision(3) << total_avg_ms
            << ", total_decode_min_ms=" << std::fixed << std::setprecision(3) << total_min_ms
            << ", total_decode_max_ms=" << std::fixed << std::setprecision(3) << total_max_ms
            << '\n';
        decode_bench_file_.flush();
    }

    stats_window_begin_ = now;
    stats_window_packets_ = 0;
    stats_window_non_zero_bytes_ = 0;
    stats_window_min_non_zero_ = std::numeric_limits<size_t>::max();
    stats_window_max_non_zero_ = 0;
    loss_window_received_ = 0;
    loss_window_missing_ = 0;
    reassembly_window_resync_ = 0;
    reassembly_window_au_dropped_ = 0;
    decode_window_packets_ = 0;
    decode_window_frames_ = 0;
    decode_window_ns_ = 0;
    decode_window_max_ns_ = 0;
    decode_window_samples_ns_.clear();
}

VtxMqttStreamProcessor::TestMode VtxMqttStreamProcessor::ParseTestModeFromEnv()
{
    const char *env = std::getenv("VTX_DECODE_TEST_MODE");
    if (env == nullptr)
    {
        return TestMode::kFullPipeline;
    }
    TestMode mode = TestMode::kFullPipeline;
    if (TryParseTestMode(env, mode))
    {
        return mode;
    }
    return TestMode::kFullPipeline;
}

bool VtxMqttStreamProcessor::TryParseTestMode(const std::string &raw, TestMode &mode)
{
    if (raw.empty())
    {
        return false;
    }
    std::string normalized = raw;
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                   [](unsigned char c)
                   { return static_cast<char>(std::tolower(c)); });
    if (normalized == "recv" || normalized == "receive_only")
    {
        mode = TestMode::kReceiveOnly;
        return true;
    }
    if (normalized == "reassemble" || normalized == "reassemble_only")
    {
        mode = TestMode::kReassembleOnly;
        return true;
    }
    if (normalized == "full" || normalized == "full_pipeline")
    {
        mode = TestMode::kFullPipeline;
        return true;
    }
    return false;
}

const char *VtxMqttStreamProcessor::TestModeName(TestMode mode)
{
    switch (mode)
    {
    case TestMode::kReceiveOnly:
        return "receive_only";
    case TestMode::kReassembleOnly:
        return "reassemble_only";
    default:
        return "full_pipeline";
    }
}

void VtxMqttStreamProcessor::VideoWriterLoop()
{
    cv::VideoWriter writer;
    std::chrono::steady_clock::time_point last_ts;
    bool has_last = false;

    while (true)
    {
        TimedFrame item;
        {
            std::unique_lock<std::mutex> lock(video_queue_mutex_);
            video_queue_cv_.wait(lock, [&]()
                                 { return video_writer_stop_.load() || !video_queue_.empty(); });
            if (video_queue_.empty())
            {
                if (video_writer_stop_.load())
                {
                    break;
                }
                continue;
            }
            item = std::move(video_queue_.front());
            video_queue_.pop_front();
        }

        if (item.frame_bgr.empty())
        {
            continue;
        }

        if (!writer.isOpened())
        {
            if (!writer.open(
                    video_path_.string(),
                    cv::VideoWriter::fourcc('M', 'J', 'P', 'G'),
                    kVideoOutputFps,
                    item.frame_bgr.size(),
                    true))
            {
                LOG_ERROR("Failed to open mqtt realtime video output: {}", video_path_.string());
                break;
            }
            LOG_INFO("MQTT realtime video output: {}", video_path_.string());
        }

        int repeat = 1;
        if (has_last)
        {
            const auto delta = item.timestamp - last_ts;
            const double delta_sec = std::chrono::duration<double>(delta).count();
            repeat = std::max(1, static_cast<int>(std::lround(delta_sec * kVideoOutputFps)));
        }

        for (int i = 0; i < repeat; ++i)
        {
            writer.write(item.frame_bgr);
        }

        last_ts = item.timestamp;
        has_last = true;
    }

    if (writer.isOpened())
    {
        writer.release();
    }

    const uint64_t dropped = video_drop_count_.load();
    if (dropped > 0)
    {
        LOG_WARN("MQTT realtime video writer dropped {} frames due to queue pressure", dropped);
    }
}

} // namespace hrvtx::standalone
