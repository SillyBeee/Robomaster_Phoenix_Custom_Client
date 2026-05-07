#include "callback_center.hpp"
#include "component_manager.hpp"
#include "driver_gamepad.hpp"
#include "driver_mqtt.hpp"
#include "driver_socket.hpp"
#include "raw300_decoder/vtx_mqtt_stream_processor.hpp"
#include "scheduler_center/scheduler_manager.h"
#include "utils_json_refactor.hpp"
#include "video_pipeline.hpp"
#include "video_source.hpp"
#include "logger.hpp"
#include <app-window.h>
#include <filesystem>

static SchedulerCenterConfig MakeSchedulerConfig() {
    SchedulerCenterConfig cfg;
    cfg.type = SchedulerCenterConfig::Type::CHOREOGRAPHY;
    cfg.choreography.choreo_thread_num = 3;
    cfg.choreography.pool_thread_num = 2;
    // 三个专用线程的核亲和度
    cfg.choreography.thread_affinities = {
        {0},  // [0] Socket PollLoop → core 0
        {1},  // [1] Video Decode    → core 1
        {2}   // [2] GamePad Poll    → core 2
    };
    // 三个专用线程的调度策略与优先级
    cfg.choreography.thread_policies = {
        {SCHED_FIFO, 80},   // [0] Socket: RT → sudo运行生效, 否则自动降级SCHED_OTHER
        {SCHED_FIFO, 70},   // [1] Decode: RT → sudo运行生效, 否则自动降级SCHED_OTHER
        {SCHED_OTHER, 0}    // [2] GamePad: 普通 — 10ms轮询, 不关键
    };
    return cfg;
}

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

    // ── 调度器初始化 ──
    auto sched_config = MakeSchedulerConfig();
    auto& scheduler = SchedulerManager::GetInstance();
    scheduler.Start(sched_config);

    auto vtx = SetupVtxProcessor(mqtt_client, COMPONENT_MANAGER.GetSettings(),
                                  source_path / "image");

    gamepad.Init();
    // 手柄轮询线程绑定到 core 2, SCHED_OTHER
    gamepad.SetThreadAffinity(sched_config.choreography.thread_affinities[2]);
    gamepad.SetThreadPolicy(sched_config.choreography.thread_policies[2].first,
                            sched_config.choreography.thread_policies[2].second);

    mqtt_client.Connect();

    auto socket_source = std::make_shared<SocketVideoSource>(socket_receiver);
    std::shared_ptr<VideoSource> vtx_source;
    if (vtx)
        vtx_source = std::make_shared<VtxVideoSource>(*vtx);

    VideoPipeline video_pipeline(callback_factory);
    video_pipeline.Start(*socket_source);
    // 视频解码线程绑定到 core 1, SCHED_FIFO priority 70
    video_pipeline.SetThreadAffinity(sched_config.choreography.thread_affinities[1]);
    video_pipeline.SetThreadPolicy(sched_config.choreography.thread_policies[1].first,
                                   sched_config.choreography.thread_policies[1].second);

    // Socket PollLoop 线程绑定到 core 0, SCHED_FIFO priority 80
    socket_receiver.SetThreadAffinity(sched_config.choreography.thread_affinities[0]);
    socket_receiver.SetThreadPolicy(sched_config.choreography.thread_policies[0].first,
                                    sched_config.choreography.thread_policies[0].second);

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
        // 切换后重新绑核
        video_pipeline.SetThreadAffinity(sched_config.choreography.thread_affinities[1]);
        video_pipeline.SetThreadPolicy(sched_config.choreography.thread_policies[1].first,
                                       sched_config.choreography.thread_policies[1].second);
    });

    main_window->run();

    video_pipeline.Stop();
    scheduler.Stop();
    if (vtx) vtx->stop();
}
