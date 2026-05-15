#include "callback_center.hpp"
#include "component_manager.hpp"
#include "driver_gamepad.hpp"
#include "driver_mqtt.hpp"
#include "driver_socket.hpp"
#include "driver_urdf_renderer.hpp"
#include "raw300_decoder/vtx_mqtt_stream_processor.hpp"
#include "scheduler_center/scheduler_manager.h"
#include "utils_json_refactor.hpp"
#include "utils_cv.hpp"
#include "video_pipeline.hpp"
#include "video_source.hpp"
#include "logger.hpp"
#include <app-window.h>
#include <filesystem>
#include <thread>
#include <chrono>

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
//  URDF 加载与渲染循环
// ============================================================
struct UrdfRenderState {
    std::unique_ptr<drivers::URDFRendererPlugin> plugin;
    std::thread render_thread;
    std::atomic<bool> stop{false};
};

static void StartUrdfRenderLoop(UrdfRenderState& state, const Callback_Factory& factory) {
    state.render_thread = std::thread([&]() {
        int frame_count = 0;
        while (!state.stop) {
            if (state.plugin->renderFrame() == drivers::URDF_SUCCESS) {
                try {
                    cv::Mat rgba = state.plugin->getImageAsMat();
                    cv::Mat bgr;
                    cv::cvtColor(rgba, bgr, cv::COLOR_RGBA2BGR);
                    if (frame_count == 0) {
                        LOG_INFO("URDF first frame: {}x{}", bgr.cols, bgr.rows);
                    }
                    auto image = MatToSlintImage(bgr);
                    slint::invoke_from_event_loop([&factory, image]() {
                        factory.set_urdf_frame(image);
                    });
                    ++frame_count;
                } catch (const std::exception& e) {
                    LOG_ERROR("URDF render failed: {}", e.what());
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    });
}

static void StopUrdfRenderLoop(UrdfRenderState& state) {
    state.stop = true;
    if (state.render_thread.joinable()) {
        state.render_thread.join();
    }
}

static std::unique_ptr<UrdfRenderState>
SetupURDF(const std::filesystem::path& src, const ClientConfig& settings,
          const Callback_Factory& factory) {
    auto state = std::make_unique<UrdfRenderState>();
    state->plugin = std::make_unique<drivers::URDFRendererPlugin>();
    drivers::UrdfRenderConfig cfg;
    cfg.width = 800;
    cfg.height = 600;
    cfg.transparent_background = true;
    cfg.anti_aliasing = 0;
    if (!state->plugin->initialize(&cfg)) {
        LOG_ERROR("URDF renderer init failed: {}", state->plugin->getLastError());
        return nullptr;
    }
    auto urdf_path = src / settings.urdf_path;
    if (state->plugin->loadURDF(urdf_path.string()) != drivers::URDF_SUCCESS) {
        LOG_ERROR("URDF load failed: {} - {}", urdf_path.string(), state->plugin->getLastError());
        return nullptr;
    }
    LOG_INFO("URDF loaded: {}", urdf_path.string());
    drivers::UrdfCameraConfig cam;
    cam.position[0] = -1.0f; cam.position[1] = -0.5f; cam.position[2] = 3.0f;
    cam.look_at[0] = 0.0f; cam.look_at[1] = 0.0f; cam.look_at[2] = 0.5f;
    cam.up[0] = 0.0f; cam.up[1] = 0.0f; cam.up[2] = 1.0f;
    cam.fov_degrees = 50.0f;
    cam.near_clip = 0.02f;
    cam.far_clip = 20.0f;
    state->plugin->setCamera(cam);
    StartUrdfRenderLoop(*state, factory);
    return state;
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

    // ── 设置 ──
    COMPONENT_MANAGER.Init(cb);
    COMPONENT_MANAGER.LoadSettings(config_path.string());
    COMPONENT_MANAGER.LoadComponents(components_path.string());
    const auto& settings = COMPONENT_MANAGER.GetSettings();

    // ── 驱动 ──
    drivers::GamePad gamepad;
    // drivers::MqttClient mqtt_client("192.168.12.1", 3333, settings.default_mqtt_client_id);
    // drivers::SocketImageReceiver socket_receiver("192.168.12.2", 3334, 16 * 1024 * 1024);
    drivers::MqttClient mqtt_client("127.0.0.1", 3333, settings.default_mqtt_client_id);
    drivers::SocketImageReceiver socket_receiver("127.0.0.1", 3334, 16 * 1024 * 1024);

    RegisterCallbacks(cb, main_window, mqtt_client, components_path);


    // ── URDF ──
    auto urdf = SetupURDF(src, settings, cb);

    // ── 调度器 ──
    auto sched_cfg = MakeSchedulerConfig();
    SchedulerManager::GetInstance().Start(sched_cfg);

    // ── VTX ──
    auto vtx = SetupVtxProcessor(mqtt_client, settings, src / "image");

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
    mqtt_client.Disconnect();
    if (urdf) StopUrdfRenderLoop(*urdf);
}
