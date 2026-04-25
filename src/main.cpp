#include "callback_center.hpp"
#include "component_manager.hpp"
#include "driver_ffmpeg_decoder.hpp"
#include "driver_gamepad.hpp"
#include "driver_mqtt.hpp"
#include "driver_socket.hpp"
#include "filesystem"
#include "logger.hpp"
#include "slint.h"
#include "utils_cv.hpp"
#include <app-window.h>
#include <opencv2/opencv.hpp>
#include <slint_image.h>
#include <atomic>
#include <cstring>
#include <mutex>
#include <thread>

namespace
{
slint::Image MatToSlintImage(const cv::Mat& mat)
{
    cv::Mat rgb;
    if (mat.type() == CV_8UC3)
    {
        cv::cvtColor(mat, rgb, cv::COLOR_BGR2RGB);
    }
    else if (mat.type() == CV_8UC4)
    {
        cv::cvtColor(mat, rgb, cv::COLOR_BGRA2RGB);
    }
    else if (mat.type() == CV_8UC1)
    {
        cv::cvtColor(mat, rgb, cv::COLOR_GRAY2RGB);
    }
    else
    {
        return slint::Image();
    }

    const int w = rgb.cols;
    const int h = rgb.rows;
    slint::SharedPixelBuffer<slint::Rgb8Pixel> buf(static_cast<uint32_t>(w), static_cast<uint32_t>(h));
    auto dst = buf.begin();

    if (rgb.isContinuous())
    {
        const uint8_t* src = rgb.data;
        const size_t pixels = static_cast<size_t>(w) * static_cast<size_t>(h);
        for (size_t i = 0; i < pixels; ++i)
        {
            dst[i] = slint::Rgb8Pixel { src[i * 3], src[i * 3 + 1], src[i * 3 + 2] };
        }
    }
    else
    {
        for (int y = 0; y < h; ++y)
        {
            const uint8_t* row = rgb.ptr<uint8_t>(y);
            for (int x = 0; x < w; ++x)
            {
                const size_t i = static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x);
                dst[i] = slint::Rgb8Pixel { row[x * 3], row[x * 3 + 1], row[x * 3 + 2] };
            }
        }
    }

    return slint::Image(std::move(buf));
}
}

int main()
{
    //加载参数
    std::filesystem::path source_path(PROJECT_SOURCE_DIR);
    std::filesystem::path config_path = source_path / "config" / "client_setting.json";
    std::filesystem::path components_path = source_path / "config" / "config.json";

    auto main_window = MainWindow::create();
    auto&& callback_factory = main_window->global<Callback_Factory>();

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

    COMPONENT_MANAGER.Init(callback_factory);
    COMPONENT_MANAGER.LoadSettings(config_path.string());
    COMPONENT_MANAGER.LoadComponents(components_path.string());
    // pose_test_slider(callback_factory);

    drivers::GamePad gamepad;
    drivers::MqttClient mqtt_client("192.168.12.1",3333, "3");
    drivers::SocketImageReceiver socket_receiver("192.168.12.2", 3334, 16 * 1920 * 1080);

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
    
    // 实例化解码器
    HevcDecoder decoder;
    drivers::SocketImageReceiver::Frame frame;
    int empty_count = 0;

    while (!stop_flag) {
      if (socket_receiver.GetFrameBlocking(frame, 500)) {
        empty_count = 0;
        
        cv::Mat img;
        // 使用 FFmpeg 解码器替代 imdecode
        // cv::Mat img = cv::imdecode(frame, cv::IMREAD_COLOR); 
        if (decoder.decode(frame, img) && !img.empty()) {
          LOG_DEBUG("SocketThread decoded image size={}x{}", img.cols, img.rows);
          {
            std::lock_guard<std::mutex> lock(ui_frame_mutex);
            latest_ui_frame = MatToSlintImage(img);
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
        } else {
          LOG_WARN("Decode failed or image empty, bytes={}", frame.size());
        }
      } else {
        // 超时或 receiver 已停止
        ++empty_count;
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
    // if(mqtt_thread.joinable()) mqtt_thread.join();
    LOG_INFO("Application exiting");
    return 0;
}