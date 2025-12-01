#ifndef DRIVER_SOCKET_HPP
#define DRIVER_SOCKET_HPP

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <iostream>
#include <map>
#include <mutex>
#include <opencv2/opencv.hpp>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace drivers
{

class SocketImageReceiver
{
public:
    using Frame = std::vector<uint8_t>;

    explicit SocketImageReceiver(const std::string& ip = "192.168.12.1", uint16_t port = 3334, size_t max_frame_size = 8 * 1024 * 1024);
    ~SocketImageReceiver();

    bool Connect();
    void Disconnect();

    // 非阻塞尝试取一帧（如果有返回 true）
    bool TryGetFrame(Frame& out);
    bool TryGetFrame(cv::Mat& out);

    // 阻塞取一帧，等待时间 timeout_ms 毫秒，timeout_ms < 0 表示无限等待
    bool GetFrameBlocking(Frame& out, int timeout_ms = 1000);
    bool GetFrameBlocking(cv::Mat& out, int timeout_ms = 1000);

private:
    struct FrameBuffer
    {
        uint16_t seq;
        uint32_t total_size = 0;
        std::map<uint16_t, std::vector<uint8_t>> fragments; // 有序按 fragment index
        size_t received_bytes = 0;
        std::chrono::steady_clock::time_point last_update;
    };

    void PollLoop();
    void CleanupStaleBuffers();

    uint16_t port_;
    size_t max_frame_size_;
    std::string ip_; 
    int sockfd_ = -1;
    bool running_ = false;
    std::thread core_thread_;

    // 重组相关
    std::unordered_map<uint16_t, FrameBuffer> buffers_;
    mutable std::mutex buffers_mutex_;

    // 已完成帧队列
    std::deque<Frame> ready_frames_;
    mutable std::mutex ready_mutex_;
    std::condition_variable ready_cv_;

    // 统计状态
    uint64_t total_packets_received_ = 0;
    uint64_t total_frames_completed_ = 0;
    uint16_t last_seq_ = 0;
};
} // namespace drivers

#endif // DRIVER_SOCKET_HPP