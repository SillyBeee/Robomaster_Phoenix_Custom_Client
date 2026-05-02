#include "callback_center.hpp"
#include "component_manager.hpp"
#include "driver_ffmpeg_decoder.hpp"
#include "driver_gamepad.hpp"
#include "driver_mqtt.hpp"
#include "driver_socket.hpp"
#include "raw300_decoder/mat_decoder.hpp"
#include "filesystem"
#include "logger.hpp"
#include "slint.h"
#include "utils_cv.hpp"
#include <app-window.h>
#include <opencv2/opencv.hpp>
#include <slint_image.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <fstream>
#include <functional>
#include <iomanip>
#include <limits>
#include <mutex>
#include <sstream>
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
    std::filesystem::create_directories(mqtt_image_dir);
    const std::filesystem::path mqtt_packet_dump_path = mqtt_image_dir / "mqtt_packets_300b.txt";
    const std::filesystem::path mqtt_non_zero_stats_path = mqtt_image_dir / "mqtt_non_zero_stats.txt";
    auto mqtt_packet_dump_file = std::make_shared<std::ofstream>(mqtt_packet_dump_path, std::ios::out | std::ios::trunc);
    auto mqtt_non_zero_stats_file = std::make_shared<std::ofstream>(mqtt_non_zero_stats_path, std::ios::out | std::ios::trunc);
    auto mqtt_packet_dump_mutex = std::make_shared<std::mutex>();
    auto mqtt_non_zero_stats_mutex = std::make_shared<std::mutex>();
    if (!mqtt_packet_dump_file->is_open())
    {
        LOG_ERROR("Failed to open mqtt packet dump file: {}", mqtt_packet_dump_path.string());
    }
    else
    {
        LOG_INFO("MQTT packet dump enabled: {}", mqtt_packet_dump_path.string());
    }
    if (!mqtt_non_zero_stats_file->is_open())
    {
        LOG_ERROR("Failed to open mqtt non-zero stats file: {}", mqtt_non_zero_stats_path.string());
    }
    else
    {
        LOG_INFO("MQTT non-zero stats file enabled: {}", mqtt_non_zero_stats_path.string());
    }
    auto raw300_decoder = std::make_shared<hrvtx::standalone::MatDecoder>();
    auto mqtt_saved_frame_count = std::make_shared<std::atomic_uint64_t>(0);
    std::string decoder_err;
    if (!raw300_decoder->start(decoder_err))
    {
        LOG_ERROR("Failed to start raw300 decoder: {}", decoder_err);
    }
    else
    {
        mqtt_client.SetCustomByteBlockHandler(
            [raw300_decoder, mqtt_image_dir, mqtt_saved_frame_count, mqtt_packet_dump_file, mqtt_packet_dump_mutex, mqtt_non_zero_stats_file, mqtt_non_zero_stats_mutex](const std::vector<uint8_t>& packet_data)
            {   
                const size_t non_zero_bytes = static_cast<size_t>(
                    std::count_if(packet_data.begin(), packet_data.end(), [](uint8_t b) { return b != 0; }));
                using Clock = std::chrono::steady_clock;
                static auto stats_window_begin = Clock::now();
                static uint64_t stats_window_packets = 0;
                static uint64_t stats_window_non_zero_bytes = 0;
                static size_t stats_window_min_non_zero = std::numeric_limits<size_t>::max();
                static size_t stats_window_max_non_zero = 0;
                ++stats_window_packets;
                stats_window_non_zero_bytes += non_zero_bytes;
                stats_window_min_non_zero = std::min(stats_window_min_non_zero, non_zero_bytes);
                stats_window_max_non_zero = std::max(stats_window_max_non_zero, non_zero_bytes);
                const auto stats_now = Clock::now();
                if (stats_now - stats_window_begin >= std::chrono::seconds(1))
                {
                    const double avg_non_zero =
                        (stats_window_packets > 0)
                            ? static_cast<double>(stats_window_non_zero_bytes) / static_cast<double>(stats_window_packets)
                            : 0.0;
                    if (mqtt_non_zero_stats_file && mqtt_non_zero_stats_file->is_open())
                    {
                        std::lock_guard<std::mutex> lock(*mqtt_non_zero_stats_mutex);
                        (*mqtt_non_zero_stats_file)
                            << "pkts=" << stats_window_packets
                            << ", avg_non_zero_bytes=" << std::fixed << std::setprecision(1) << avg_non_zero
                            << ", min=" << stats_window_min_non_zero
                            << ", max=" << stats_window_max_non_zero << '\n';
                        mqtt_non_zero_stats_file->flush();
                    }
                    stats_window_begin = stats_now;
                    stats_window_packets = 0;
                    stats_window_non_zero_bytes = 0;
                    stats_window_min_non_zero = std::numeric_limits<size_t>::max();
                    stats_window_max_non_zero = 0;
                }

                if (mqtt_packet_dump_file && mqtt_packet_dump_file->is_open())
                {
                    std::ostringstream line;
                    line << std::hex << std::setfill('0');
                    for (size_t i = 0; i < packet_data.size(); ++i)
                    {
                        if (i > 0)
                        {
                            line << ' ';
                        }
                        line << std::setw(2) << static_cast<unsigned int>(packet_data[i]);
                    }
                    std::lock_guard<std::mutex> lock(*mqtt_packet_dump_mutex);
                    (*mqtt_packet_dump_file) << line.str() << '\n';
                    mqtt_packet_dump_file->flush();
                }

                const bool is_compat_inner_frame =
                    (packet_data.size() == 300 &&
                     packet_data.size() >= 4 &&
                     packet_data.front() == static_cast<uint8_t>('s') &&
                     packet_data.back() == static_cast<uint8_t>('e'));
                const char* packet_format = is_compat_inner_frame ? "compat_inner_frame" : "raw";
                LOG_INFO("Decoding CustomByteBlock size={} bytes, packet_format={}",
                         packet_data.size(), packet_format);
                auto out = raw300_decoder->decode_packet(packet_data);
                if (out.packet_dropped)
                {
                    LOG_WARN("Drop raw300 packet: {}", out.message);
                    return;
                }
                for (const auto& frame : out.frames_bgr)
                {
                    if (frame.empty())
                    {
                        continue;
                    }
                    const uint64_t frame_id = mqtt_saved_frame_count->fetch_add(1) + 1;
                    const std::filesystem::path image_path =
                        mqtt_image_dir / ("mqtt_frame_" + std::to_string(frame_id) + ".jpg");
                    if (!cv::imwrite(image_path.string(), frame))
                    {
                        LOG_WARN("Failed to save mqtt decoded frame: {}", image_path.string());
                    }
                }
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
        //   LOG_WARN("Decode failed or image empty, bytes={}", frame.size());
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
