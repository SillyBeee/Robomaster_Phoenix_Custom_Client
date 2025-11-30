#include "callback_center.hpp"
#include "driver_gamepad.hpp"
#include "driver_mqtt.hpp"
#include "driver_socket.hpp"
#include "slint.h"
#include "spdlog/spdlog.h"
#include "utils_cv.hpp"
#include <app-window.h>
#include <opencv2/opencv.hpp>
#include <slint_image.h>
#include <thread>

int main() {
  spdlog::set_level(spdlog::level::debug);                // 日志级别
  spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v"); // 格式
  auto main_window = MainWindow::create();

  auto &callback_factory = main_window->global<Callback_Factory>();

  callback_factory.on_open_url(
      [](slint::SharedString url) { callback_open_url(url); });

  callback_factory.on_set_resolution(
      [&main_window](slint::SharedString resolution) {
        callback_set_resolution(main_window, resolution);
      });

  callback_factory.on_set_fullscreen([&main_window](bool is_fullscreen) {
    callback_set_fullscreen(main_window, is_fullscreen);
  });

  auto handle = main_window;

  std::thread cap_thread([handle] {
    // cv::VideoCapture cap("/home/ma/视频/录屏/1.webm");  // 或本地文件
    // if (!cap.isOpened()) return;
    // for (;;) {
    //     cv::Mat frame;
    //     if (!cap.read(frame)) break;
    //     slint::Image image = mat_to_slint_image(frame);
    //     slint::invoke_from_event_loop([handle, image] {
    //         handle->global<Callback_Factory>().set_video_frame(image);
    //     });
    // }
  });
  drivers::GamePad gamepad;
  drivers::MqttClient mqtt_client("127.0.0.1");
  
  drivers::SocketImageReceiver socket_receiver(3334, 1032, "127.0.0.1");

  mqtt_client.Connect();


  std::atomic_bool stop_flag{false};
  std::thread socket_thread([&socket_receiver, &stop_flag]{
    if (!socket_receiver.Init()) {
      spdlog::error("Failed to initialize SocketImageReceiver");
      return;
    }
    drivers::SocketImageReceiver::Frame frame;
    int empty_count = 0;
    while (!stop_flag) {
      // 阻塞等待一帧，超时 500ms，可根据需要调整
      // spdlog::info("cd while 1");
      if (socket_receiver.GetFrameBlocking(frame, 500)) {
        empty_count = 0;
        // 假设收到的是 JPEG/PNG 压缩字节流，使用 imdecode 解码
        cv::Mat img = cv::imdecode(frame, cv::IMREAD_COLOR);
        if (!img.empty()) {
          spdlog::info("SocketThread displayed image size={}x{}", img.cols, img.rows);
          cv::imshow("SocketFrame", img);
          // 1ms 处理窗体事件
          cv::waitKey(1);
          // spdlog::info("SocketThread displayed image size={}x{}", img.cols, img.rows);
        } else {
          spdlog::warn("SocketThread decoded image is empty, bytes={}", frame.size());
        }
      } else {
        // 超时或 receiver 已停止
        ++empty_count;
        if (empty_count % 5 == 0) { // 每 20 次（约10s）打印一次，避免过多日志
          spdlog::debug("SocketThread waiting for frames... (no frame in last {} checks)", empty_count);
        }
      }
    }
    // cv::destroyWindow("SocketFrame");
    spdlog::info("Socket display thread exiting");
  });

  
  

  main_window->run();
  
  spdlog::info("UI exited, shutting down socket thread");
  stop_flag = true;
  socket_receiver.Shutdown();
  if (socket_thread.joinable()) socket_thread.join();
  // if(mqtt_thread.joinable()) mqtt_thread.join();
  if (cap_thread.joinable()) cap_thread.join();
  spdlog::info("Application exiting");
  return 0;
}