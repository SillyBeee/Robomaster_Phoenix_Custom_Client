#include "drivers/driver_socket.hpp"

#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <errno.h>
#include <algorithm>

// 新增日志
#include <spdlog/spdlog.h>

namespace drivers
{

SocketImageReceiver::SocketImageReceiver(uint16_t port, size_t max_frame_size, const std::string &bind_ip)
    : port_(port),
      max_frame_size_(max_frame_size),
      bind_ip_(bind_ip)
{
}

SocketImageReceiver::~SocketImageReceiver()
{
    Shutdown();
}

bool SocketImageReceiver::Init()
{
    if (running_) {
        spdlog::warn("SocketImageReceiver::Init called but already running");
        return true; // 已经初始化
    }

    spdlog::info("SocketImageReceiver::Init bind_ip='{}' port={}", bind_ip_.empty() ? "0.0.0.0" : bind_ip_, port_);

    sockfd_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd_ < 0) {
        spdlog::error("SocketImageReceiver::Init: socket creation failed: {}", strerror(errno));
        return false;
    }

    // 重用地址
    int yes = 1;
    if (setsockopt(sockfd_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0) {
        spdlog::error("SocketImageReceiver::Init: setsockopt SO_REUSEADDR failed: {}", strerror(errno));
        close(sockfd_);
        sockfd_ = -1;
        return false;
    }

    // 绑定到指定 IP 或 INADDR_ANY:port_
    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port_);

    if (!bind_ip_.empty()) {
        // 将 bind_ip_ 转换为 binary address
        if (inet_pton(AF_INET, bind_ip_.c_str(), &addr.sin_addr) != 1) {
            spdlog::error("SocketImageReceiver::Init: invalid bind IP: {}", bind_ip_);
            close(sockfd_);
            sockfd_ = -1;
            return false;
        }
    } else {
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
    }

    if (bind(sockfd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        spdlog::error("SocketImageReceiver::Init: bind failed: {}", strerror(errno));
        close(sockfd_);
        sockfd_ = -1;
        return false;
    }

    spdlog::info("SocketImageReceiver bound successfully");

    // 将 socket 设为 non-blocking，以便可优雅地退出线程
    int flags = fcntl(sockfd_, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(sockfd_, F_SETFL, flags | O_NONBLOCK);
    }

    running_ = true;
    core_thread_ = std::thread(&SocketImageReceiver::PollLoop, this);

    return true;
}

void SocketImageReceiver::Shutdown()
{
    if (!running_) {
        spdlog::info("SocketImageReceiver::Shutdown called but not running");
        return;
    }

    spdlog::info("SocketImageReceiver::Shutdown");
    running_ = false;

    // 关闭 socket：Polling thread 会检测到错误并退出
    if (sockfd_ >= 0) {
        close(sockfd_);
        sockfd_ = -1;
    }

    // 唤醒可能阻塞等待的 GetFrameBlocking
    {
        std::lock_guard<std::mutex> lk(ready_mutex_);
        ready_cv_.notify_all();
    }

    if (core_thread_.joinable()) {
        core_thread_.join();
    }

    // 清理内存
    {
        std::lock_guard<std::mutex> lk1(buffers_mutex_);
        buffers_.clear();
    }
    {
        std::lock_guard<std::mutex> lk2(ready_mutex_);
        ready_frames_.clear();
    }
    spdlog::info("SocketImageReceiver::Shutdown finished");
}

bool SocketImageReceiver::TryGetFrame(Frame &out)
{
    std::lock_guard<std::mutex> lk(ready_mutex_);
    if (ready_frames_.empty()) return false;
    out = std::move(ready_frames_.front());
    ready_frames_.pop_front();
    return true;
}

bool SocketImageReceiver::GetFrameBlocking(Frame &out, int timeout_ms)
{
    std::unique_lock<std::mutex> lk(ready_mutex_);
    if (timeout_ms < 0) {
        ready_cv_.wait(lk, [this] { return !ready_frames_.empty() || !running_; });
    } else {
        ready_cv_.wait_for(lk, std::chrono::milliseconds(timeout_ms), [this] { return !ready_frames_.empty() || !running_; });
    }

    if (ready_frames_.empty()) return false;
    out = std::move(ready_frames_.front());
    ready_frames_.pop_front();
    return true;
}

void SocketImageReceiver::PrintState(std::ostream &os) const
{
    std::lock_guard<std::mutex> b_lk(buffers_mutex_);
    std::lock_guard<std::mutex> r_lk(ready_mutex_);
    os << "SocketImageReceiver: port=" << port_
       << " running=" << running_
       << " sockfd=" << sockfd_
       << " total_packets=" << total_packets_received_
       << " total_frames_completed=" << total_frames_completed_
       << " ready_frames=" << ready_frames_.size()
       << " active_buffers=" << buffers_.size()
       << " last_seq=" << last_seq_
       << "\n";
}

void SocketImageReceiver::CleanupStaleBuffers()
{
    auto now = std::chrono::steady_clock::now();
    const auto stale_threshold = std::chrono::milliseconds(2000);

    std::lock_guard<std::mutex> lk(buffers_mutex_);

    for (auto it = buffers_.begin(); it != buffers_.end();) {
        if (now - it->second.last_update > stale_threshold) {
            it = buffers_.erase(it);
        } else {
            ++it;
        }
    }
}

void SocketImageReceiver::PollLoop()
{
    // buffer for UDP read
    const size_t recv_buf_size = 65536;
    std::vector<uint8_t> recv_buf(recv_buf_size);

    sockaddr_in src_addr;
    socklen_t addrlen = sizeof(src_addr);

    while (running_) {
        ssize_t n = recvfrom(sockfd_, recv_buf.data(), recv_buf_size, 0, reinterpret_cast<sockaddr*>(&src_addr), &addrlen);
        if (n < 0) {
            if (errno == EWOULDBLOCK || errno == EAGAIN) {
                // 没有数据，稍微休眠
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                CleanupStaleBuffers();
                continue;
            } else if (!running_) {
                break;
            } else {
                // 其它错误，打印并继续
                spdlog::error("SocketImageReceiver::PollLoop recvfrom error: {}", strerror(errno));
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
        }

        if (static_cast<size_t>(n) < 8) {
            // header 都没有，丢弃
            ++total_packets_received_;
            spdlog::warn("SocketImageReceiver::PollLoop received too-small packet: {} bytes", n);
            continue;
        }

        ++total_packets_received_;
        spdlog::trace("SocketImageReceiver::PollLoop packet received {} bytes from {}", n, inet_ntoa(src_addr.sin_addr));

        // parse header：seq(uint16_t), frag_index(uint16_t), total_size(uint32_t) — 全部 network order（big endian）
        const uint8_t* p = recv_buf.data();
        uint16_t seq_net = (uint16_t)((p[0] << 8) | p[1]);
        uint16_t frag_idx_net = (uint16_t)((p[2] << 8) | p[3]);
        uint32_t total_size_net = (uint32_t)((p[4] << 24) | (p[5] << 16) | (p[6] << 8) | p[7]);

        uint16_t seq = ntohs(seq_net); // 网络字节序转 host
        uint16_t frag_idx = ntohs(frag_idx_net);
        uint32_t total_size = ntohl(total_size_net);

        // payload
        size_t payload_len = static_cast<size_t>(n) - 8;
        const uint8_t* payload_ptr = p + 8;

        if (total_size == 0 || total_size > max_frame_size_) {
            // 非法大小，丢弃
            spdlog::warn("SocketImageReceiver::PollLoop invalid total_size={} (max={})", total_size, max_frame_size_);
            continue;
        }

        // 存储 fragment
        {
            std::lock_guard<std::mutex> lk(buffers_mutex_);
            auto& fb = buffers_[seq];

            // 如果是新建 buffer，则初始化
            if (fb.fragments.empty() && fb.received_bytes == 0) {
                fb.seq = seq;
                fb.total_size = total_size;
                fb.last_update = std::chrono::steady_clock::now();
            } else {
                // total_size 与现有不一致，则丢弃该分片（可能是旧残留）
                if (fb.total_size != total_size) {
                    spdlog::warn("SocketImageReceiver::PollLoop fragment total_size mismatch for seq={}, expected={}, got={}", seq, fb.total_size, total_size);
                    continue;
                }
            }

            // 插入 fragment（防止重复）
            if (fb.fragments.find(frag_idx) == fb.fragments.end()) {
                fb.fragments[frag_idx] = std::vector<uint8_t>(payload_ptr, payload_ptr + payload_len);
                fb.received_bytes += payload_len;
                fb.last_update = std::chrono::steady_clock::now();
            }

            // 如果已接收到所有字节，则尝试重组
            if (fb.received_bytes >= fb.total_size) {
                // 按 frag_idx 顺序拼接
                Frame frame;
                frame.reserve(fb.total_size);

                for (const auto& kv : fb.fragments) {
                    const auto& frag = kv.second;
                    frame.insert(frame.end(), frag.begin(), frag.end());
                }

                // 若拼接后的大小仍大于 expected，则 trim（避免异常）
                if (frame.size() > fb.total_size) {
                    frame.resize(fb.total_size);
                }

                // 将完成帧推入 ready 队列
                {
                    std::lock_guard<std::mutex> lk2(ready_mutex_);
                    ready_frames_.emplace_back(std::move(frame));
                    ++total_frames_completed_;
                    last_seq_ = seq;
                    spdlog::info("SocketImageReceiver::PollLoop completed frame seq={} size={}", seq, fb.total_size);
                }
                ready_cv_.notify_one();

                // 删除 fb
                buffers_.erase(seq);
            }
        }
    } // while

    // 关闭时清理
    {
        std::lock_guard<std::mutex> lk(buffers_mutex_);
        buffers_.clear();
    }
    {
        std::lock_guard<std::mutex> lk(ready_mutex_);
        ready_frames_.clear();
    }
    spdlog::info("SocketImageReceiver::PollLoop exited");
}

} // namespace drivers