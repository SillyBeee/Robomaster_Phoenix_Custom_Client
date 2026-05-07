#ifndef CALLBACK_CENTER_HPP
#define CALLBACK_CENTER_HPP
#include "slint_string.h"
#include <app-window.h>
#include <opencv2/opencv.hpp>

namespace drivers
{
class MqttClient;
}

void callback_open_url(slint::SharedString url);

void callback_set_resolution(slint::ComponentHandle<MainWindow>& window, slint::SharedString resolution);

void callback_set_fullscreen(slint::ComponentHandle<MainWindow>& window, bool is_fullscreen);

void callback_minimize_window(slint::ComponentHandle<MainWindow>& window);

void callback_maximize_window(slint::ComponentHandle<MainWindow>& window , bool is_maximized);

void callback_close_window(slint::ComponentHandle<MainWindow>& window);

void callback_move_window(slint::ComponentHandle<MainWindow>& window, float dx, float dy);

bool callback_apply_mqtt_config(drivers::MqttClient& mqtt_client,
                                slint::SharedString ip,
                                slint::SharedString port,
                                slint::SharedString client_id);




// 全局变量用于存储滑动条的值
static int g_roll = 1800;
static int g_pitch = 1800;
static int g_yaw = 1800;
// 保存 factory 的指针
static const Callback_Factory* g_factory_ptr = nullptr;

static void on_trackbar_change(int, void*) {
    if (!g_factory_ptr) return;

    // 映射 0-3600 到 -180.0 到 180.0 度
    float r_deg = (g_roll - 1800) / 10.0f;
    float p_deg = (g_pitch - 1800) / 10.0f;
    float y_deg = (g_yaw - 1800) / 10.0f;


    Pose_slint pose;
    pose.roll = r_deg ;
    pose.pitch = p_deg ;
    pose.yaw = y_deg ;


    
    const auto* factory_ptr = g_factory_ptr;
    slint::invoke_from_event_loop([=]() {
        factory_ptr->set_current_pose(pose);
    });
}

inline void pose_test_slider(const Callback_Factory& factory){
    g_factory_ptr = &factory; // 保存指针

    cv::namedWindow("Pose Debug", cv::WINDOW_AUTOSIZE);
    
    // 创建滑动条: 范围 0-3600, 初始值 1800 (对应 0 度)
    cv::createTrackbar("Roll (x10)", "Pose Debug", &g_roll, 3600, on_trackbar_change);
    cv::createTrackbar("Pitch (x10)", "Pose Debug", &g_pitch, 3600, on_trackbar_change);
    cv::createTrackbar("Yaw (x10)", "Pose Debug", &g_yaw, 3600, on_trackbar_change);

    // 触发一次初始更新
    // on_trackbar_change(0, nullptr);
}
#endif // CALLBACK_CENTER_HPP
