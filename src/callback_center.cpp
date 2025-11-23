#include "callback_center.hpp"

void callback_open_url(slint::SharedString url)
{
    std::string url_str(url);
    std::string command;
    #if defined(_WIN32) || defined(_WIN64)
    // Windows
    command = "start " + url_str;
    #else
    // Linux 和其他 Unix 系统(macos不支持喵)
    command = "xdg-open '" + url_str + "'";
    #endif
    
    int result = std::system(command.c_str());
    if (result == 0) {
        std::cout << "Successfully opened URL: " << url_str << std::endl;
    } else {
        std::cerr << "Failed to open URL: " << url_str << std::endl;
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
        // window->set_window_height(height);
        // window->set_window_width(width);
        if (width > 0 && height > 0) {
            slint::LogicalSize size{};
            size.width = float(width);
            size.height = float(height);
            window->window().set_size(size);
            window->global<Callback_Factory>()
                .set_font_size(std::min(float(width), float(height)) * 0.015f);
            std::cout<<"Set font size to: "<< std::min(float(width), float(height)) * 0.015f <<std::endl;
            
        }
    }
    catch (const std::exception& e) {
            std::cerr << "Failed to parse resolution: " << e.what() << std::endl;
    }
    
}

void callback_set_fullscreen(slint::ComponentHandle<MainWindow>& window, 
                            bool is_fullscreen){
    window->set_is_fullscreen(is_fullscreen);
    window->window().set_fullscreen(is_fullscreen);
    
}
