#pragma once

#include "driver_ffmpeg_decoder.hpp"
#include "driver_socket.hpp"
#include "raw300_decoder/vtx_mqtt_stream_processor.hpp"
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <opencv2/opencv.hpp>

class VideoSource {
public:
    virtual ~VideoSource() = default;
    virtual bool Open() = 0;
    virtual void Close() = 0;
    virtual bool ReadFrame(cv::Mat& frame, int timeout_ms = 500) = 0;
    virtual std::string_view Name() const = 0;
};

class SocketVideoSource : public VideoSource {
public:
    explicit SocketVideoSource(drivers::SocketImageReceiver& receiver);
    bool Open() override;
    void Close() override;
    bool ReadFrame(cv::Mat& frame, int timeout_ms = 500) override;
    std::string_view Name() const override { return "socket"; }

private:
    drivers::SocketImageReceiver* receiver_;
    HevcDecoder decoder_;
};

class VtxVideoSource : public VideoSource {
public:
    explicit VtxVideoSource(hrvtx::standalone::VtxMqttStreamProcessor& processor);
    ~VtxVideoSource();
    bool Open() override;
    void Close() override;
    bool ReadFrame(cv::Mat& frame, int timeout_ms = 500) override;
    std::string_view Name() const override { return "vtx"; }

    void OnFrame(cv::Mat frame);

private:
    hrvtx::standalone::VtxMqttStreamProcessor* processor_;
    std::deque<cv::Mat> queue_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool active_ = false;
};
