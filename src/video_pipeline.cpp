#include "video_pipeline.hpp"
#include "logger.hpp"
#include "utils_cv.hpp"
#include <opencv2/opencv.hpp>

VideoPipeline::VideoPipeline(const Callback_Factory& factory)
    : factory_(&factory) {}

VideoPipeline::~VideoPipeline() {
    Stop();
}

void VideoPipeline::Start(VideoSource& source) {
    if (thread_.joinable()) return;
    stop_ = false;
    thread_ = std::thread(&VideoPipeline::DecodeLoop, this, &source);
}

void VideoPipeline::Stop() {
    if (!thread_.joinable()) return;
    stop_ = true;
    thread_.join();
}

void VideoPipeline::SwitchSource(VideoSource& new_source) {
    if (thread_.joinable()) {
        stop_ = true;
        thread_.join();
        stop_ = false;
        thread_ = std::thread(&VideoPipeline::DecodeLoop, this, &new_source);
    }
}

void VideoPipeline::DecodeLoop(VideoSource* source) {
    if (!source->Open()) {
        LOG_ERROR("Failed to open video source: {}", source->Name());
        return;
    }

    cv::Size last_good_size(1280, 720);
    int empty_count = 0;

    while (!stop_) {
        cv::Mat frame;
        if (source->ReadFrame(frame, 500)) {
            empty_count = 0;
            last_good_size = frame.size();
            PostUiFrame(frame);
        } else {
            ++empty_count;
            if (empty_count >= 2) {
                cv::Mat blank(last_good_size.height, last_good_size.width, CV_8UC3, cv::Scalar(0, 0, 0));
                PostUiFrame(blank);
            }
        }
    }

    source->Close();
    LOG_INFO("Video pipeline thread exiting");
}

void VideoPipeline::PostUiFrame(const cv::Mat& mat) {
    {
        std::lock_guard<std::mutex> lock(ui_frame_mutex_);
        latest_ui_frame_ = MatToSlintImage(mat);
    }
    if (!ui_update_pending_.exchange(true)) {
        slint::invoke_from_event_loop([this]() {
            slint::Image frame;
            {
                std::lock_guard<std::mutex> lock(ui_frame_mutex_);
                frame = latest_ui_frame_;
            }
            factory_->set_video_frame(frame);
            ui_update_pending_.store(false);
        });
    }
}
