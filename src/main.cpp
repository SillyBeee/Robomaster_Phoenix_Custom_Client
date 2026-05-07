#include "callback_center.hpp"
#include "component_manager.hpp"
#include "driver_gamepad.hpp"
#include "driver_mqtt.hpp"
#include "driver_socket.hpp"
#include "raw300_decoder/vtx_mqtt_stream_processor.hpp"
#include "utils_json_refactor.hpp"
#include "video_pipeline.hpp"
#include "video_source.hpp"
#include "logger.hpp"
#include <app-window.h>
#include <filesystem>

// Scheduler integration:
//   #include "scheduler_center/scheduler_manager.h"
//   auto& scheduler = SchedulerManager::GetInstance();
//   SchedulerCenterConfig sched_config{};
//   sched_config.type = SchedulerCenterConfig::Type::CLASSIC;
//   sched_config.thread_num = 4;
//   scheduler.Start(sched_config);

static std::shared_ptr<hrvtx::standalone::VtxMqttStreamProcessor>
SetupVtxProcessor(drivers::MqttClient& mqtt_client,
                  const ClientConfig& settings,
                  const std::filesystem::path& image_dir) {
    auto processor = std::make_shared<hrvtx::standalone::VtxMqttStreamProcessor>(
        image_dir, settings.decode_test_mode,
        settings.h264_wait_for_sps_pps, settings.h264_wait_for_idr);
    std::string err;
    if (!processor->start(err)) {
        LOG_ERROR("Failed to start raw300 decoder: {}", err);
        return nullptr;
    }
    mqtt_client.SetCustomByteBlockHandler(
        [processor](const std::vector<uint8_t>& data) {
            processor->OnPacket(data);
        });
    return processor;
}

int main() {
    auto source_path = std::filesystem::path(PROJECT_SOURCE_DIR);
    auto config_path = source_path / "config" / "client_setting.json";
    auto components_path = source_path / "config" / "config.json";

    auto main_window = MainWindow::create();
    auto&& callback_factory = main_window->global<Callback_Factory>();

    drivers::GamePad gamepad;
    drivers::MqttClient mqtt_client("192.168.12.1", 3333, "3");
    drivers::SocketImageReceiver socket_receiver("192.168.12.2", 3334, 16 * 1024 * 1024);

    RegisterCallbacks(callback_factory, main_window, mqtt_client, components_path);

    COMPONENT_MANAGER.Init(callback_factory);
    COMPONENT_MANAGER.LoadSettings(config_path.string());
    COMPONENT_MANAGER.LoadComponents(components_path.string());

    auto vtx = SetupVtxProcessor(mqtt_client, COMPONENT_MANAGER.GetSettings(),
                                  source_path / "image");

    gamepad.Init();
    mqtt_client.Connect();

    auto socket_source = std::make_shared<SocketVideoSource>(socket_receiver);
    std::shared_ptr<VideoSource> vtx_source;
    if (vtx)
        vtx_source = std::make_shared<VtxVideoSource>(*vtx);

    VideoPipeline video_pipeline(callback_factory);
    video_pipeline.Start(*socket_source);

    // Video source toggle
    std::shared_ptr<VideoSource> active_source = socket_source;
    callback_factory.on_toggle_video_source([&]() {
        if (active_source.get() == socket_source.get() && vtx_source) {
            active_source = vtx_source;
        } else {
            active_source = socket_source;
        }
        video_pipeline.SwitchSource(*active_source);
        callback_factory.set_video_source_name(slint::SharedString(active_source->Name()));
        LOG_INFO("Switched video source to: {}", active_source->Name());
    });

    // Scheduler integration:
    //   scheduler.Start(sched_config);

    main_window->run();

    video_pipeline.Stop();
    if (vtx) vtx->stop();
}
