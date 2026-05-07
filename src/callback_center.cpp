#include "callback_center.hpp"
#include "component_manager.hpp"
#include "driver_mqtt.hpp"
#include "logger.hpp"
void callback_open_url(slint::SharedString url)
{
    std::string url_str(url);
    std::string command;
    #if defined(_WIN32) || defined(_WIN64)
    // Windows
    command = "start " + url_str;
    #else
    // Linux 和其他 Unix 系统(macos不支持喵)
    command = "xdg-open '" + url_str + "' > /dev/null 2>&1";
    #endif
    
    int result = std::system(command.c_str());
    if (result == 0) {
        LOG_INFO("Successfully opened URL: {}", url_str);
    } else {
        LOG_ERROR("Failed to open URL: {}", url_str);
    }
}

void callback_set_resolution(slint::ComponentHandle<MainWindow>& window, slint::SharedString resolution)
{
    std::string res_str(resolution);
    size_t pos = res_str.find('x');
    int width = 0, height = 0;
    try{
        if (pos != std::string::npos) {
        width = std::stoi(res_str.substr(0, pos));
        height = std::stoi(res_str.substr(pos + 1));
        }
        if (width > 0 && height > 0) {
            slint::LogicalSize size{};
            size.width = float(width);
            size.height = float(height);
            window->window().set_size(size);
            float font_size = std::min(float(width), float(height)) * 0.015f;
            window->global<Callback_Factory>().set_font_size(font_size);
            LOG_INFO("Set font size to: {:.2f}", static_cast<double>(font_size));
            LOG_INFO("Resolution set to: {}x{}", width, height);
        }
    }
    catch (const std::exception& e) {
            LOG_ERROR("Failed to parse resolution: {}", e.what());
    }
    
}

void callback_set_fullscreen(slint::ComponentHandle<MainWindow>& window, 
                            bool is_fullscreen){
    window->set_is_fullscreen(is_fullscreen);
    window->window().set_fullscreen(is_fullscreen);
    LOG_INFO("Set fullscreen mode to: {}", is_fullscreen);
    
}

void callback_minimize_window(slint::ComponentHandle<MainWindow>& window){
    window->window().set_minimized(true);
    LOG_INFO("Window minimized");
}

void callback_maximize_window(slint::ComponentHandle<MainWindow>& window , bool is_maximized){
    window->window().set_maximized(is_maximized);
    LOG_INFO("Window maximized");
}

void callback_close_window(slint::ComponentHandle<MainWindow>& window){
    slint::quit_event_loop(); 
    LOG_INFO("Window closed");
}

void callback_move_window(slint::ComponentHandle<MainWindow> &window, float dx, float dy)
{   
    auto pos = window->window().position();
    window->window().set_position(slint::PhysicalPosition({
        pos.x + static_cast<int>(dx), 
        pos.y + static_cast<int>(dy)
    }));
}

bool callback_apply_mqtt_config(drivers::MqttClient& mqtt_client,
                                slint::SharedString ip,
                                slint::SharedString port,
                                slint::SharedString client_id)
{
    const std::string ip_str(ip);
    const std::string port_str(port);
    const std::string client_id_str(client_id);

    int port_num = 0;
    try
    {
        port_num = std::stoi(port_str);
    }
    catch (const std::exception& e)
    {
        LOG_ERROR("MQTT config rejected, invalid port '{}': {}", port_str, e.what());
        return false;
    }

    if (ip_str.empty() || client_id_str.empty() || port_num <= 0 || port_num > 65535)
    {
        LOG_ERROR("MQTT config rejected, ip/client_id/port invalid. ip='{}', client_id='{}', port={}",
                  ip_str, client_id_str, port_num);
        return false;
    }

    mqtt_client.Disconnect();
    mqtt_client.SetConfig(ip_str, port_num, client_id_str);
    const bool connect_ok = mqtt_client.Connect();
    LOG_INFO("MQTT apply config result={} target={}:{} client_id={}", connect_ok, ip_str, port_num, client_id_str);
    return connect_ok;
}

void RegisterCallbacks(const Callback_Factory& factory,
                       slint::ComponentHandle<MainWindow> window,
                       drivers::MqttClient& mqtt_client,
                       const std::filesystem::path& components_path) {
    factory.on_open_url([](slint::SharedString url) {
        callback_open_url(url);
    });
    factory.on_set_resolution([window](slint::SharedString resolution) mutable {
        callback_set_resolution(window, resolution);
    });
    factory.on_set_fullscreen([window](bool is_fullscreen) mutable {
        callback_set_fullscreen(window, is_fullscreen);
    });
    factory.on_minimize_window([window]() mutable {
        callback_minimize_window(window);
    });
    factory.on_maximize_window([window](bool is_maximized) mutable {
        callback_maximize_window(window, is_maximized);
    });
    factory.on_close_window([window]() mutable {
        callback_close_window(window);
    });
    factory.on_move_window([window](float dx, float dy) mutable {
        callback_move_window(window, dx, dy);
    });
    factory.on_save_to_json([&components_path]() {
        COMPONENT_MANAGER.SaveComponents(components_path.string());
    });
    factory.on_apply_mqtt_config([&mqtt_client](slint::SharedString ip,
                                                 slint::SharedString port,
                                                 slint::SharedString client_id) {
        return callback_apply_mqtt_config(mqtt_client, ip, port, client_id);
    });
}
