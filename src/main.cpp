#include "callback_center.hpp"
#include "driver_gamepad.hpp"
#include "slint.h"
#include "spdlog/spdlog.h"
#include "utils_cv.hpp"
#include <app-window.h>
#include <opencv2/opencv.hpp>
#include <slint_image.h>
#include <thread>

int main() {
  spdlog::set_level(spdlog::level::info);                // 日志级别
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
  if (gamepad.Init()) {
    std::thread([&gamepad] {
      for (;;) {
        gamepad.PrintState();
        std::this_thread::sleep_for(std::chrono::seconds(1));
      }
    }).detach();
  }

  main_window->run();
  cap_thread.join();
  return 0;
}