#include "callback_center.hpp"
#include "component_manager.hpp"
#include "driver_ffmpeg_decoder.hpp"
#include "driver_gamepad.hpp"
#include "driver_mqtt.hpp"
#include "driver_socket.hpp"
#include "raw300_decoder/vtx_mqtt_stream_processor.hpp"
#include "filesystem"
#include "logger.hpp"
#include "slint.h"
#include "utils_cv.hpp"
#include "utils_json_refactor.hpp"
#include <app-window.h>
#include <opencv2/opencv.hpp>
#include <slint_image.h>
#include <atomic>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>



int main()
{
    //加载参数
    std::filesystem::path source_path(PROJECT_SOURCE_DIR);
    std::filesystem::path config_path = source_path / "config" / "client_setting.json";
    std::filesystem::path components_path = source_path / "config" / "config.json";

    auto main_window = MainWindow::create();
    auto&& callback_factory = main_window->global<Callback_Factory>();
    drivers::GamePad gamepad;
    drivers::MqttClient mqtt_client("192.168.12.1", 3333, "3");
    drivers::SocketImageReceiver socket_receiver("192.168.12.2", 3334, 16 * 1024 * 1024);

    callback_factory.on_open_url(
        [](slint::SharedString url)
        { callback_open_url(url); }
    );
    callback_factory.on_set_resolution(
        [&main_window](slint::SharedString resolution)
        {
            callback_set_resolution(main_window, resolution);
        }
    );
    callback_factory.on_set_fullscreen([&main_window](bool is_fullscreen)
                                       { callback_set_fullscreen(main_window, is_fullscreen); });
    callback_factory.on_minimize_window([&main_window]()
                                        { callback_minimize_window(main_window); });
    callback_factory.on_maximize_window([&main_window](bool is_maximized)
                                        { callback_maximize_window(main_window, is_maximized); });
    callback_factory.on_close_window([&main_window]()
                                    { callback_close_window(main_window); });
    callback_factory.on_move_window([&main_window](float dx, float dy)
                                    { callback_move_window(main_window, dx, dy); });
    callback_factory.on_save_to_json([&callback_factory, &components_path]()
                                     { COMPONENT_MANAGER.SaveComponents(components_path.string()); });
    callback_factory.on_apply_mqtt_config(std::bind(callback_apply_mqtt_config,
                                                    std::ref(mqtt_client),
                                                    std::placeholders::_1,
                                                    std::placeholders::_2,
                                                    std::placeholders::_3));

    COMPONENT_MANAGER.Init(callback_factory);
    COMPONENT_MANAGER.LoadSettings(config_path.string());
    COMPONENT_MANAGER.LoadComponents(components_path.string());
    // pose_test_slider(callback_factory);

    const std::filesystem::path mqtt_image_dir = source_path / "image";
    auto vtx_stream_processor = std::make_shared<hrvtx::standalone::VtxMqttStreamProcessor>(
        mqtt_image_dir, COMPONENT_MANAGER.GetSettings().decode_test_mode,
        COMPONENT_MANAGER.GetSettings().h264_wait_for_sps_pps, COMPONENT_MANAGER.GetSettings().h264_wait_for_idr);
    std::string decoder_err;

    if (!vtx_stream_processor->start(decoder_err))
    {
        LOG_ERROR("Failed to start raw300 decoder: {}", decoder_err);
    }
    else
    {
        mqtt_client.SetCustomByteBlockHandler(
            [vtx_stream_processor](const std::vector<uint8_t>& packet_data)
            {
                vtx_stream_processor->OnPacket(packet_data);
            }
        );
    }

    gamepad.Init();
    mqtt_client.Connect();

    std::atomic_bool stop_flag { false };
    auto* callback_factory_ptr = &callback_factory;
    std::mutex ui_frame_mutex;
    slint::Image latest_ui_frame;
    std::atomic_bool ui_update_pending { false };

    std::thread socket_thread([&socket_receiver, &stop_flag, callback_factory_ptr, &ui_frame_mutex, &latest_ui_frame, &ui_update_pending]
                              {
    if (!socket_receiver.Connect()) {
      LOG_ERROR("Failed to initialize SocketImageReceiver");
      return;
    }
    
    // 实例化解码器，连续失败时会重建以清理参考帧状态
    auto decoder = std::make_unique<HevcDecoder>();
    drivers::SocketImageReceiver::Frame frame;
    int empty_count = 0;
    int decode_fail_count = 0;
    cv::Size last_good_size(1280, 720);
    constexpr int kClearUiNoFrameChecks = 2;          // 2 * 500ms = 1s
    constexpr int kDecoderResetFailThreshold = 30;    // 连续失败后重建解码器

    auto post_ui_frame = [&](const cv::Mat& mat) {
      {
        std::lock_guard<std::mutex> lock(ui_frame_mutex);
        latest_ui_frame = MatToSlintImage(mat);
      }
      if (!ui_update_pending.exchange(true)) {
        slint::invoke_from_event_loop([callback_factory_ptr, &ui_frame_mutex, &latest_ui_frame, &ui_update_pending]() {
          slint::Image frame_for_ui;
          {
            std::lock_guard<std::mutex> lock(ui_frame_mutex);
            frame_for_ui = latest_ui_frame;
          }
          callback_factory_ptr->set_video_frame(frame_for_ui);
          ui_update_pending.store(false);
        });
      }
    };

    auto clear_ui = [&]() {
      cv::Mat blank(last_good_size.height, last_good_size.width, CV_8UC3, cv::Scalar(0, 0, 0));
      post_ui_frame(blank);
    };

    while (!stop_flag) {
      if (socket_receiver.GetFrameBlocking(frame, 500)) {
        empty_count = 0;
        
        cv::Mat img;
        // 使用 FFmpeg 解码器替代 imdecode
        // cv::Mat img = cv::imdecode(frame, cv::IMREAD_COLOR); 
        if (decoder->decode(frame, img) && !img.empty()) {
          decode_fail_count = 0;
          last_good_size = img.size();
          LOG_DEBUG("SocketThread decoded image size={}x{}", img.cols, img.rows);
          post_ui_frame(img);
        } else {
          ++decode_fail_count;
          if (decode_fail_count >= kDecoderResetFailThreshold) {
            LOG_WARN("SocketThread decode failed {} times, resetting HEVC decoder", decode_fail_count);
            decoder = std::make_unique<HevcDecoder>();
            decode_fail_count = 0;
            clear_ui();
          }
        }
      } else {
        // 超时或 receiver 已停止
        ++empty_count;
        if (empty_count >= kClearUiNoFrameChecks) {
          clear_ui();
        }
        if (empty_count % 5 == 0) { // 每 20 次（约10s）打印一次，避免过多日志
          LOG_DEBUG("SocketThread waiting for frames... (no frame in last {} checks)", empty_count);
        }
      }
    }
    LOG_INFO("Socket display thread exiting"); });

    main_window->run();

    LOG_INFO("UI exited, shutting down socket thread");
    stop_flag = true;
    if (socket_thread.joinable())
    {
        socket_thread.join();
    }
    vtx_stream_processor->stop();

    // if(mqtt_thread.joinable()) mqtt_thread.join();
    LOG_INFO("Application exiting");
    return 0;
}
