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

// ============================================================
//  Helper: 为组件线程应用调度器配置（亲和度 + 调度策略）
// ============================================================
template<typename T>
static void ApplyThreadConfig(T& component, const SchedulerCenterConfig& cfg, int idx) {
    component.SetThreadAffinity(cfg.choreography.thread_affinities[idx]);
    component.SetThreadPolicy(cfg.choreography.thread_policies[idx].first,
                              cfg.choreography.thread_policies[idx].second);
}

// ============================================================
//  VTX 流处理器初始化
// ============================================================
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

// ============================================================
//  调度器配置（CHOREOGRAPHY 模式）
//      idx [0] Socket PollLoop — 最低延迟处理 UDP 包
//      idx [1] Video Decode    — HEVC 解码（CPU 密集）
//      idx [2] GamePad Poll    — 10ms 轮询，不关键
// ============================================================
static SchedulerCenterConfig MakeSchedulerConfig() {
    SchedulerCenterConfig cfg;
    cfg.type = SchedulerCenterConfig::Type::CHOREOGRAPHY;
    cfg.choreography.choreo_thread_num = 3;
    cfg.choreography.pool_thread_num = 2;
    cfg.choreography.thread_affinities = {{0}, {1}, {2}};
    cfg.choreography.thread_policies = {
        {SCHED_FIFO, 80},   // [0] Socket — sudo 下生效，否则自动降级
        {SCHED_FIFO, 70},   // [1] Decode — same
        {SCHED_OTHER, 0}    // [2] GamePad
    };
    return cfg;
}

// ============================================================
//  视频源切换回调
// ============================================================
static void SetupVideoToggle(const Callback_Factory& factory,
                              VideoPipeline& pipeline,
                              std::shared_ptr<VideoSource> socket_src,
                              std::shared_ptr<VideoSource> vtx_src,
                              const SchedulerCenterConfig& cfg) {
    auto active = socket_src;
    factory.on_toggle_video_source([&, socket_src, vtx_src]() mutable {
        active = (active.get() == socket_src.get() && vtx_src) ? vtx_src : socket_src;
        pipeline.SwitchSource(*active);
        factory.set_video_source_name(slint::SharedString(active->Name()));
        ApplyThreadConfig(pipeline, cfg, 1);
        LOG_INFO("Switched video source to: {}", active->Name());
    });
}

// ============================================================
//  入口
// ============================================================
int main() {
    // ── 路径 ──
    auto src = std::filesystem::path(PROJECT_SOURCE_DIR);
    auto config_path = src / "config" / "client_setting.json";
    auto components_path = src / "config" / "config.json";

    // ── UI ──
    auto main_window = MainWindow::create();
    auto&& cb = main_window->global<Callback_Factory>();

    // ── 驱动 ──
    drivers::GamePad gamepad;
    drivers::MqttClient mqtt_client("192.168.12.1", 3333, "3");
    drivers::SocketImageReceiver socket_receiver("192.168.12.2", 3334, 16 * 1024 * 1024);

    RegisterCallbacks(cb, main_window, mqtt_client, components_path);

    COMPONENT_MANAGER.Init(cb);
    COMPONENT_MANAGER.LoadSettings(config_path.string());
    COMPONENT_MANAGER.LoadComponents(components_path.string());

    // ── 调度器 ──
    auto sched_cfg = MakeSchedulerConfig();
    SchedulerManager::GetInstance().Start(sched_cfg);

    // ── VTX ──
    auto vtx = SetupVtxProcessor(mqtt_client, COMPONENT_MANAGER.GetSettings(), src / "image");

    // ── 手柄 ──
    gamepad.Init();
    ApplyThreadConfig(gamepad, sched_cfg, 2);

    // ── MQTT ──
    mqtt_client.Connect();

    // ── 视频管线 ──
    auto socket_src = std::make_shared<SocketVideoSource>(socket_receiver);
    std::shared_ptr<VideoSource> vtx_src;
    if (vtx) vtx_src = std::make_shared<VtxVideoSource>(*vtx);

    VideoPipeline video_pipeline(cb);
    video_pipeline.Start(*socket_src);
    ApplyThreadConfig(video_pipeline, sched_cfg, 1);

    // Socket PollLoop 属于 SocketImageReceiver 内部线程
    ApplyThreadConfig(socket_receiver, sched_cfg, 0);

    // ── 视频源切换 ──
    SetupVideoToggle(cb, video_pipeline, socket_src, vtx_src, sched_cfg);

    // ── 运行 ──
    main_window->run();

    // ── 清理 ──
    video_pipeline.Stop();
    SchedulerManager::GetInstance().Stop();
    if (vtx) vtx->stop();
}
