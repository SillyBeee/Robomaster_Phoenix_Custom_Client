#include "video_source.hpp"
#include "logger.hpp"

// ---- SocketVideoSource ----

SocketVideoSource::SocketVideoSource(drivers::SocketImageReceiver& receiver)
    : receiver_(&receiver) {}

bool SocketVideoSource::Open() {
    return receiver_->Connect();
}

void SocketVideoSource::Close() {
    receiver_->Disconnect();
}

bool SocketVideoSource::ReadFrame(cv::Mat& frame, int timeout_ms) {
    drivers::SocketImageReceiver::Frame raw;
    if (!receiver_->GetFrameBlocking(raw, timeout_ms))
        return false;

    if (!decoder_.decode(raw, frame) || frame.empty())
        return false;

    return true;
}

// ---- VtxVideoSource ----

VtxVideoSource::VtxVideoSource(hrvtx::standalone::VtxMqttStreamProcessor& processor)
    : processor_(&processor) {}

VtxVideoSource::~VtxVideoSource() {
    Close();
}

bool VtxVideoSource::Open() {
    active_ = true;
    processor_->SetFrameCallback([this](cv::Mat frame) {
        OnFrame(std::move(frame));
    });
    return true;
}

void VtxVideoSource::Close() {
    active_ = false;
    processor_->SetFrameCallback(nullptr);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.clear();
    }
    cv_.notify_all();
}

void VtxVideoSource::OnFrame(cv::Mat frame) {
    if (!active_) return;
    std::lock_guard<std::mutex> lock(mutex_);
    if (queue_.size() >= 10)
        queue_.pop_front();
    queue_.push_back(std::move(frame));
    cv_.notify_one();
}

bool VtxVideoSource::ReadFrame(cv::Mat& frame, int timeout_ms) {
    std::unique_lock<std::mutex> lock(mutex_);
    if (!cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                      [this] { return !queue_.empty() || !active_; }))
        return false;

    if (queue_.empty())
        return false;

    frame = std::move(queue_.front());
    queue_.pop_front();
    return true;
}
