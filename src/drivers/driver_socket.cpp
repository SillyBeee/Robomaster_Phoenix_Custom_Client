#include "drivers/driver_socket.hpp"

#include "logger.hpp"
#include <algorithm>
#include <arpa/inet.h>
#include <cstring>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace drivers
{

SocketImageReceiver::SocketImageReceiver(const std::string& ip, uint16_t port, size_t max_frame_size): port_(port),
                                                                                                       max_frame_size_(max_frame_size),
                                                                                                       ip_(ip)
{
}

SocketImageReceiver::~SocketImageReceiver()
{
    this->Disconnect();
}

bool SocketImageReceiver::Connect()
{
    if (running_)
        return true;

    // 创建 UDP Socket
    sockfd_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sockfd_ < 0)
    {
        LOG_ERROR("Socket creation failed: {}", strerror(errno));
        return false;
    }

    int yes = 1;
    setsockopt(sockfd_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    int rcvbuf = 4 * 1024 * 1024;
    setsockopt(sockfd_, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));

    sockaddr_in local_addr;
    memset(&local_addr, 0, sizeof(local_addr));
    local_addr.sin_family = AF_INET;
    local_addr.sin_port = htons(port_); // 必须是 3334

    // 如果是本地测试，建议绑定 127.0.0.1 或者 0.0.0.0 (INADDR_ANY)
    if (!ip_.empty())
    {
        inet_pton(AF_INET, ip_.c_str(), &local_addr.sin_addr);
    }
    else
    {
        local_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    }

    if (bind(sockfd_, (sockaddr*)&local_addr, sizeof(local_addr)) < 0)
    {
        LOG_ERROR("Bind failed on port {}: {}", port_, strerror(errno));
        close(sockfd_);
        return false;
    }

    // 5. 设置非阻塞
    int flags = fcntl(sockfd_, F_GETFL, 0);
    fcntl(sockfd_, F_SETFL, flags | O_NONBLOCK);

    LOG_INFO("Socket successfully BOUND to {}:{}", ip_.empty() ? "ANY" : ip_, port_);

    running_ = true;
    core_thread_ = std::thread(&SocketImageReceiver::PollLoop, this);
    return true;
}

void SocketImageReceiver::Disconnect()
{
    if (!running_)
    {
        LOG_INFO("SocketImageReceiver::Disconnect called but not running");
        return;
    }

    LOG_INFO("SocketImageReceiver::Disconnect");
    running_ = false;

    // 关闭 socket：Polling thread 会检测到错误并退出
    if (sockfd_ >= 0)
    {
        close(sockfd_);
        sockfd_ = -1;
    }

    // 唤醒可能阻塞等待的 GetFrameBlocking
    {
        std::lock_guard<std::mutex> lk(ready_mutex_);
        ready_cv_.notify_all();
    }

    if (core_thread_.joinable())
    {
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
    LOG_INFO("SocketImageReceiver::Shutdown finished");
}

bool SocketImageReceiver::TryGetFrame(Frame& out)
{
    std::lock_guard<std::mutex> lk(ready_mutex_);
    if (ready_frames_.empty())
        return false;
    out = std::move(ready_frames_.front());
    ready_frames_.pop_front();
    return true;
}

bool SocketImageReceiver::GetFrameBlocking(Frame& out, int timeout_ms)
{
    std::unique_lock<std::mutex> lk(ready_mutex_);
    if (timeout_ms < 0)
    {
        ready_cv_.wait(lk, [this]
                       { return !ready_frames_.empty() || !running_; });
    }
    else
    {
        ready_cv_.wait_for(lk, std::chrono::milliseconds(timeout_ms), [this]
                           { return !ready_frames_.empty() || !running_; });
    }

    if (ready_frames_.empty())
        return false;
    out = std::move(ready_frames_.front());
    ready_frames_.pop_front();
    return true;
}

void SocketImageReceiver::CleanupStaleBuffers()
{
    auto now = std::chrono::steady_clock::now();
    const auto stale_threshold = std::chrono::milliseconds(2000);

    std::lock_guard<std::mutex> lk(buffers_mutex_);

    for (auto it = buffers_.begin(); it != buffers_.end();)
    {
        if (now - it->second.last_update > stale_threshold)
        {
            it = buffers_.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

void SocketImageReceiver::PollLoop()
{
    // buffer for UDP read
    const size_t recv_buf_size = 65536;
    std::vector<uint8_t> recv_buf(recv_buf_size);
    LOG_INFO("SocketImageReceiver::PollLoop started");

    while (running_)
    {
        int n = recv(sockfd_, recv_buf.data(), recv_buf.size(), 0);
        if (n < 0)
        {
            if (errno == EWOULDBLOCK || errno == EAGAIN)
            {
                // 没有数据，稍微休眠
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                CleanupStaleBuffers();
                continue;
            }
            else if (!running_)
            {
                break;
            }
            else
            {
                // 其它错误，打印并继续
                LOG_ERROR("SocketImageReceiver::PollLoop recvfrom error: {}", strerror(errno));
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
        }

        if (static_cast<size_t>(n) < 8)
        {
            // header 都没有，丢弃
            ++total_packets_received_;
            LOG_WARN("SocketImageReceiver::PollLoop received too-small packet: {} bytes", n);
            continue;
        }

        ++total_packets_received_;
        LOG_INFO("SocketImageReceiver::PollLoop packet received {} bytes", n);

        // parse header：seq(uint16_t), frag_index(uint16_t), total_size(uint32_t) — 全部 network order（big endian）
        const uint8_t* p = recv_buf.data();
        uint16_t seq = (uint16_t)((p[0] << 8) | p[1]);
        uint16_t frag_idx = (uint16_t)((p[2] << 8) | p[3]);
        uint32_t total_size = (uint32_t)((p[4] << 24) | (p[5] << 16) | (p[6] << 8) | p[7]);
        LOG_ERROR("SocketImageReceiver::PollLoop header: seq={} frag_idx={} total_size={}", seq, frag_idx, total_size);

        // payload
        size_t payload_len = static_cast<size_t>(n) - 8;
        const uint8_t* payload_ptr = p + 8;

        if (total_size == 0 || total_size > max_frame_size_)
        {
            // 非法大小，丢弃
            LOG_WARN("SocketImageReceiver::PollLoop invalid total_size={} (max={})", total_size, max_frame_size_);
            continue;
        }

        // 存储 fragment
        {
            std::lock_guard<std::mutex> lk(buffers_mutex_);
            auto& fb = buffers_[seq];

            // 如果是新建 buffer，则初始化
            if (fb.fragments.empty() && fb.received_bytes == 0)
            {
                fb.seq = seq;
                fb.total_size = total_size;
                fb.last_update = std::chrono::steady_clock::now();
            }
            else
            {
                // total_size 与现有不一致，则丢弃该分片（可能是旧残留）
                if (fb.total_size != total_size)
                {
                    LOG_WARN("SocketImageReceiver::PollLoop fragment total_size mismatch for seq={}, expected={}, got={}", seq, fb.total_size, total_size);
                    continue;
                }
            }

            // 插入 fragment（防止重复）
            if (fb.fragments.find(frag_idx) == fb.fragments.end())
            {
                fb.fragments[frag_idx] = std::vector<uint8_t>(payload_ptr, payload_ptr + payload_len);
                fb.received_bytes += payload_len;
                fb.last_update = std::chrono::steady_clock::now();
            }

            // 如果已接收到所有字节，则尝试重组
            if (fb.received_bytes >= fb.total_size)
            {
                // 按 frag_idx 顺序拼接
                Frame frame;
                frame.reserve(fb.total_size);

                for (const auto& kv: fb.fragments)
                {
                    const auto& frag = kv.second;
                    frame.insert(frame.end(), frag.begin(), frag.end());
                }

                // 若拼接后的大小仍大于 expected，则 trim（避免异常）
                if (frame.size() > fb.total_size)
                {
                    frame.resize(fb.total_size);
                }

                // 将完成帧推入 ready 队列
                {
                    std::lock_guard<std::mutex> lk2(ready_mutex_);
                    ready_frames_.emplace_back(std::move(frame));
                    ++total_frames_completed_;
                    last_seq_ = seq;
                    LOG_INFO("SocketImageReceiver::PollLoop completed frame seq={} size={}", seq, fb.total_size);
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
    LOG_INFO("SocketImageReceiver::PollLoop exited");
}

} // namespace drivers