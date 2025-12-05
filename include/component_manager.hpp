#ifndef COMPONENT_MANAGER_HPP
#define COMPONENT_MANAGER_HPP
#include "slint_string.h"
#include <app-window.h>
#include <slint_color.h>


void LoadSettings(const Callback_Factory& factory,const std::string& config_path="");
void LoadComponents(const Callback_Factory& factory,std::string config_path="");

static inline slint::Color HexToColor(const std::string &hex) {

    if (hex == "transparent") {
        return slint::Color::from_argb_uint8(0, 0, 0, 0);
    }
    
    std::string s = hex;
    if (s.rfind("#", 0) == 0) {
        s = s.substr(1);
    }
    if (s.length() == 6) {
        int r = std::stoi(s.substr(0, 2), nullptr, 16);
        int g = std::stoi(s.substr(2, 2), nullptr, 16);
        int b = std::stoi(s.substr(4, 2), nullptr, 16);
        return slint::Color::from_rgb_uint8(r, g, b);
    }
    return slint::Color::from_rgb_uint8(255, 255, 255); // 默认白色
}



#endif