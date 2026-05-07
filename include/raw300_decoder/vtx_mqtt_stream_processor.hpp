#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <opencv2/core/mat.hpp>

#include "raw300_decoder/mat_decoder.hpp"

namespace hrvtx::standalone
{

class VtxMqttStreamProcessor
{
  public:
    explicit VtxMqttStreamProcessor(const std::filesystem::path &output_dir,
                                    std::string test_mode_config = "",
                                    bool h264_wait_for_sps_pps = true,
                                    bool h264_wait_for_idr = true);
    ~VtxMqttStreamProcessor();

    bool start(std::string &err);
    void stop();
    void OnPacket(const std::vector<uint8_t> &packet_data);

    enum class SeqFormat : std::uint8_t
    {
        kUnknown = 0,
        kLegacy16At1,
        kTest32At6,
    };

    enum class TestMode : std::uint8_t
    {
        kFullPipeline = 0,
        kReceiveOnly,
        kReassembleOnly,
    };

  private:
    struct TimedFrame
    {
        cv::Mat frame_bgr;
        std::chrono::steady_clock::time_point timestamp;
    };

    static std::string PacketToHexLine(const std::vector<uint8_t> &packet_data);
    void WriteWindowStatsLocked(const std::chrono::steady_clock::time_point &now);
    void VideoWriterLoop();
    static TestMode ParseTestModeFromEnv();
    static bool TryParseTestMode(const std::string &raw, TestMode &mode);
    static const char *TestModeName(TestMode mode);

    std::filesystem::path output_dir_;
    std::filesystem::path video_path_;
    std::filesystem::path packet_dump_path_;
    std::filesystem::path non_zero_stats_path_;
    std::filesystem::path abnormal_dump_path_;
    std::filesystem::path loss_stats_path_;
    std::filesystem::path decode_bench_path_;

    std::unique_ptr<MatDecoder> decoder_;
    std::atomic_bool running_{false};
    std::string test_mode_config_;
    bool h264_wait_for_sps_pps_{true};
    bool h264_wait_for_idr_{true};

    std::ofstream packet_dump_file_;
    std::ofstream non_zero_stats_file_;
    std::ofstream abnormal_dump_file_;
    std::ofstream loss_stats_file_;
    std::ofstream decode_bench_file_;
    std::mutex io_mutex_;

    TestMode test_mode_{TestMode::kFullPipeline};

    std::chrono::steady_clock::time_point stats_window_begin_{};
    uint64_t stats_window_packets_{0};
    uint64_t stats_window_non_zero_bytes_{0};
    size_t stats_window_min_non_zero_{0};
    size_t stats_window_max_non_zero_{0};

    uint64_t loss_window_received_{0};
    uint64_t loss_window_missing_{0};
    uint64_t loss_total_received_{0};
    uint64_t loss_total_missing_{0};
    uint64_t loss_total_duplicate_{0};
    uint64_t loss_total_resync_{0};
    SeqFormat seq_format_{SeqFormat::kUnknown};
    bool has_last_seq16_{false};
    uint16_t last_seq16_{0};
    bool has_last_seq32_{false};
    uint32_t last_seq32_{0};

    double warmup_duration_s_{2.0};
    bool warmup_active_{false};
    std::chrono::steady_clock::time_point warmup_end_{};
    uint64_t warmup_packets_{0};
    uint64_t warmup_missing_{0};
    uint64_t warmup_dropped_{0};
    uint64_t warmup_reassembly_resync_{0};
    uint64_t warmup_reassembly_au_dropped_{0};

    uint64_t reassembly_last_resync_{0};
    uint64_t reassembly_last_au_dropped_{0};
    uint64_t reassembly_window_resync_{0};
    uint64_t reassembly_window_au_dropped_{0};
    uint64_t reassembly_total_resync_{0};
    uint64_t reassembly_total_au_dropped_{0};
    std::chrono::steady_clock::time_point last_reassembly_warn_{};

    uint64_t decode_window_packets_{0};
    uint64_t decode_window_frames_{0};
    uint64_t decode_window_ns_{0};
    uint64_t decode_window_max_ns_{0};
    uint64_t decode_total_packets_{0};
    uint64_t decode_total_frames_{0};
    uint64_t decode_total_ns_{0};
    uint64_t decode_total_min_ns_{0};
    uint64_t decode_total_max_ns_{0};
    std::vector<uint64_t> decode_window_samples_ns_;

    std::thread video_writer_thread_;
    std::deque<TimedFrame> video_queue_;
    std::mutex video_queue_mutex_;
    std::condition_variable video_queue_cv_;
    std::atomic_bool video_writer_stop_{false};
    std::atomic_uint64_t video_drop_count_{0};
};

} // namespace hrvtx::standalone
