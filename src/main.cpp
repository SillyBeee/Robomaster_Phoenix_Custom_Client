#include "callback_center.hpp"
#include "driver_ffmpeg_decoder.hpp"
#include "driver_gamepad.hpp"
#include "driver_mqtt.hpp"
#include "driver_socket.hpp"
#include "logger.hpp"
#include "slint.h"
#include "utils_cv.hpp"
#include <app-window.h>
#include <opencv2/opencv.hpp>
#include <slint_image.h>
#include <thread>
#include "component_manager.hpp"
#include "filesystem"
int main()
{
    auto main_window = MainWindow::create();

    auto& callback_factory = main_window->global<Callback_Factory>();

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

    //加载参数
    std::filesystem::path source_path(PROJECT_SOURCE_DIR);
    std::filesystem::path config_path = source_path / "config" / "client_setting.json";
    std::filesystem::path components_path = source_path / "config" / "config.json";
    LoadSettings(callback_factory, config_path.string());
    LoadComponents(callback_factory, components_path.string());
    pose_test_slider(callback_factory);

    drivers::GamePad gamepad;
    drivers::MqttClient mqtt_client("127.0.0.1");
    drivers::SocketImageReceiver socket_receiver("127.0.0.1", 3334, 16 * 1024 * 1024);

    mqtt_client.Connect();

    std::atomic_bool stop_flag { false };
    std::thread socket_thread([&socket_receiver, &stop_flag]
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
          LOG_INFO("SocketThread displayed image size={}x{}", img.cols, img.rows);
          cv::imshow("SocketFrame", img);
          cv::waitKey(1);
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
    // cv::destroyWindow("SocketFrame");
    LOG_INFO("Socket display thread exiting");
  });

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