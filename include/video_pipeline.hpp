#pragma once

#include "driver_ffmpeg_decoder.hpp"
#include "driver_socket.hpp"
#include <app-window.h>
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

// Scheduler integration:
//   #include "scheduler_center/scheduler_base.h"
//   void SetScheduler(SchedulerBase* scheduler);

class VideoPipeline {
public:
    explicit VideoPipeline(const Callback_Factory& factory);
    ~VideoPipeline();

    void Start(drivers::SocketImageReceiver& receiver);
    void Stop();

    // Scheduler integration point:
    // Call before Start() to run the decode loop as a scheduled task.
    // void SetScheduler(SchedulerBase* scheduler);

private:
    void DecodeLoop(drivers::SocketImageReceiver* receiver);
    void PostUiFrame(const cv::Mat& mat);
    void ClearUi();

    const Callback_Factory* factory_;
    std::thread thread_;
    std::atomic_bool stop_{false};

    std::mutex ui_frame_mutex_;
    slint::Image latest_ui_frame_;
    std::atomic_bool ui_update_pending_{false};
};
