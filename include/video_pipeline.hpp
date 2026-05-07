#pragma once

#include "video_source.hpp"
#include <app-window.h>
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>

class VideoPipeline {
public:
    explicit VideoPipeline(const Callback_Factory& factory);
    ~VideoPipeline();

    void Start(VideoSource& source);
    void Stop();

    void SwitchSource(VideoSource& new_source);

private:
    void DecodeLoop(VideoSource* source);
    void PostUiFrame(const cv::Mat& mat);
    void ClearUi();

    const Callback_Factory* factory_;
    std::thread thread_;
    std::atomic_bool stop_{false};

    std::mutex ui_frame_mutex_;
    slint::Image latest_ui_frame_;
    std::atomic_bool ui_update_pending_{false};
};
