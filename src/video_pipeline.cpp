#include "video_pipeline.hpp"
#include "logger.hpp"
#include "utils_cv.hpp"
#include <opencv2/opencv.hpp>

VideoPipeline::VideoPipeline(const Callback_Factory& factory)
    : factory_(&factory) {}

VideoPipeline::~VideoPipeline() {
    Stop();
}

void VideoPipeline::Start(drivers::SocketImageReceiver& receiver) {
    if (thread_.joinable()) return;

    // Scheduler integration: instead of spawning a raw thread,
    // enqueue DecodeLoop via scheduler_->Enqueue(...).
    stop_ = false;
    thread_ = std::thread(&VideoPipeline::DecodeLoop, this, &receiver);
}

void VideoPipeline::Stop() {
    if (!thread_.joinable()) return;
    stop_ = true;
    thread_.join();
}

void VideoPipeline::DecodeLoop(drivers::SocketImageReceiver* receiver) {
    if (!receiver->Connect()) {
        LOG_ERROR("Failed to initialize SocketImageReceiver");
        return;
    }

    auto decoder = std::make_unique<HevcDecoder>();
    drivers::SocketImageReceiver::Frame frame;
    int empty_count = 0;
    int decode_fail_count = 0;
    cv::Size last_good_size(1280, 720);
    constexpr int kClearUiNoFrameChecks = 2;
    constexpr int kDecoderResetFailThreshold = 30;

    while (!stop_) {
        if (receiver->GetFrameBlocking(frame, 500)) {
            empty_count = 0;

            cv::Mat img;
            if (decoder->decode(frame, img) && !img.empty()) {
                decode_fail_count = 0;
                last_good_size = img.size();
                PostUiFrame(img);
            } else {
                ++decode_fail_count;
                if (decode_fail_count >= kDecoderResetFailThreshold) {
                    LOG_WARN("Decode failed {} times, resetting HEVC decoder", decode_fail_count);
                    decoder = std::make_unique<HevcDecoder>();
                    decode_fail_count = 0;
                    cv::Mat blank(last_good_size.height, last_good_size.width, CV_8UC3, cv::Scalar(0, 0, 0));
                    PostUiFrame(blank);
                }
            }
        } else {
            ++empty_count;
            if (empty_count >= kClearUiNoFrameChecks) {
                cv::Mat blank(last_good_size.height, last_good_size.width, CV_8UC3, cv::Scalar(0, 0, 0));
                PostUiFrame(blank);
            }
        }
    }
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

void VideoPipeline::ClearUi() {
    cv::Mat blank(720, 1280, CV_8UC3, cv::Scalar(0, 0, 0));
    PostUiFrame(blank);
}
