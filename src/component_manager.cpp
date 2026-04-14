#include "component_manager.hpp"
#include "app-window.h"
#include "logger.hpp"
#include "protocol.pb.h"
#include "utils_json_refactor.hpp"
#include <fmt/format.h>


ComponentManager& ComponentManager::GetInstance() {
    static ComponentManager instance;
    return instance;
}


void ComponentManager::Init( const Callback_Factory& factory) {
    factory_ptr_ = &factory;
}

void ComponentManager::LoadSettings(const std::string& config_path) {
    if (factory_ptr_ == nullptr) {
        LOG_ERROR("ComponentManager not initialized! Call Init() first.");
        return;
    }

    JsonUtils<ClientConfig> utils;
    if (!utils.LoadJsonFromFile(config_path)) {
        LOG_ERROR("Failed to load configuration from {}", config_path);
        return;
    }
    LOG_INFO("config: resolution={}x{}, port={}, baudrate={}",
             utils.GetConfig().resolution[0],
             utils.GetConfig().resolution[1],
             utils.GetConfig().port,
             utils.GetConfig().baudrate);
}

void ComponentManager::LoadComponents(std::string config_path) {
    if (factory_ptr_ == nullptr) {
        LOG_ERROR("ComponentManager not initialized! Call Init() first.");
        return;
    }

    auto components_vector = factory_ptr_->get_components();
    size_t count = components_vector->row_count();
    LOG_INFO("Read {} components from Callback_Factory", count);
    JsonUtils<ComponentsConfig> utils;
    if (!utils.LoadJsonFromFile(config_path)) {
        LOG_ERROR("Failed to load components configuration from {}", config_path);
        return;
    }
    auto& configs = utils.GetConfig();
    LOG_INFO("Loaded {} components from JSON file", configs.components.size());

    std::vector<ComponentData> slint_components_data;
    slint_components_data.reserve(configs.components.size());
    for (const auto& config : configs.components) {
        ComponentData data;
        data.type = config.type;
        data.rel_x = config.rel_x;
        data.rel_y = config.rel_y;
        data.rel_width = config.rel_width;
        data.rel_height = config.rel_height;
        data.color = HexToColor(config.color); // 调用私有静态成员
        data.opacity = config.opacity;
        data.layer = config.layer;
        slint_components_data.push_back(data);
    }
    auto model = std::make_shared<slint::VectorModel<ComponentData>>(slint_components_data);
    factory_ptr_->set_components(model);
    LOG_INFO("Loaded and set {} components to UI", slint_components_data.size());
}

void ComponentManager::SaveComponents(const std::string& config_path) {
    if (factory_ptr_ == nullptr) {
        LOG_ERROR("ComponentManager not initialized! Call Init() first.");
        return;
    }

    auto components_vector = factory_ptr_->get_components();
    size_t count = components_vector->row_count();
    LOG_INFO("Saving {} components to JSON file", count);

    JsonUtils<ComponentsConfig> utils;
    ComponentsConfig configs;
    for (size_t i = 0; i < count; ++i) {
        auto data = components_vector->row_data(i);
        ComponentConfig config;
        config.type = data->type;
        config.rel_x = data->rel_x;
        config.rel_y = data->rel_y;
        config.rel_width = data->rel_width;
        config.rel_height = data->rel_height;
        
        // 颜色转换逻辑
        config.color = fmt::format("#{:02X}{:02X}{:02X}",
                       static_cast<int>(data->color.color().red()),
                       static_cast<int>(data->color.color().green()),
                       static_cast<int>(data->color.color().blue()));
        if (data->color.color().alpha() == 0) {
            config.color = "transparent";
        }
        
        config.opacity = data->opacity;
        config.layer = data->layer;
        configs.components.push_back(config);
    }
    utils.SetConfig(configs);
    if (!utils.SaveJsonToFile(config_path)) {
        LOG_ERROR("Failed to save components configuration to {}", config_path);
        return;
    }
    LOG_INFO("Successfully saved {} components to {}", configs.components.size(), config_path);
}










// 私有辅助函数实现
slint::Color ComponentManager::HexToColor(const std::string &hex) {
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







//cpp后端输入slint数据更新函数实现

void ComponentManager::SetGameStatus(GameStatus input) {
    if (factory_ptr_ == nullptr) {
        LOG_ERROR("ComponentManager not initialized! Call Init() first.");
        return;
    }
    GameStatus_slint status;
    status.current_round = input.current_round();
    status.total_rounds = input.total_rounds();
    status.red_score = input.red_score();
    status.blue_score = input.blue_score();
    status.current_stage = input.current_stage();
    status.stage_countdown_sec = input.stage_countdown_sec();
    status.stage_elapsed_sec = input.stage_elapsed_sec();
    status.is_paused = input.is_paused();
    slint::invoke_from_event_loop([this, status]() {
            factory_ptr_->set_game_status(status);
    });
}

void ComponentManager::SetGlobalUnitStatus(GlobalUnitStatus input) {
    if (factory_ptr_ == nullptr) {
        LOG_ERROR("ComponentManager not initialized! Call Init() first.");
        return;
    }
    GlobalUnitStatus_slint status;
    status.base_hp = input.base_health();
    status.base_status = input.base_status();
    status.base_shield_hp = input.base_shield();
    status.outpost_hp = input.outpost_health();
    status.outpost_status = input.outpost_status();
    status.robot_hp = [this, &input]() {
        std::vector<int> robot_hp_vec;
        for (const auto& hp : input.robot_health()) {
            robot_hp_vec.push_back(hp);
        }
        return std::make_shared<slint::VectorModel<int>>(robot_hp_vec);
    }();
    status.robot_bullets_capacity = [this, &input]() {
        std::vector<int> bullets_vec;
        for (const auto& bullets : input.robot_bullets()) {
            bullets_vec.push_back(bullets);
        }
        return std::make_shared<slint::VectorModel<int>>(bullets_vec);
    }();
    status.team_damage = input.total_damage_red();
    status.enemy_damage = input.total_damage_blue();
    slint::invoke_from_event_loop([this, status]() {
            factory_ptr_->set_global_unit_status(status);

    });
}

void ComponentManager::SetGlobalLogisticsStatus(GlobalLogisticsStatus input) {
    if (factory_ptr_ == nullptr) {
        LOG_ERROR("ComponentManager not initialized! Call Init() first.");
        return;
    }
    
    GlobalLogisticsStatus_slint status;
    status.remaining_economy = input.remaining_economy();
    status.total_economy_obtained = input.total_economy_obtained();
    status.tech_level = input.tech_level();
    status.encryption_level = input.encryption_level();
    slint::invoke_from_event_loop([this, status]() {
            factory_ptr_->set_global_logistics_status(status);
    });
}


void ComponentManager::SetRobotRespawnStatus(RobotRespawnStatus input) {
    if (factory_ptr_ == nullptr) {
        LOG_ERROR("ComponentManager not initialized! Call Init() first.");
        return;
    }
    
    RobotRespawnStatus_slint status;
    status.is_pending_respawn = input.is_pending_respawn();
    status.total_respawn_progress = input.total_respawn_progress();
    status.current_respawn_progress = input.current_respawn_progress();
    status.can_free_respawn = input.can_free_respawn();
    status.gold_cost_for_respawn = input.gold_cost_for_respawn();
    status.can_pay_for_respawn = input.can_pay_for_respawn();
    slint::invoke_from_event_loop([this, status]() {
            factory_ptr_->set_robot_respawn_status(status);
    });
}

void ComponentManager::SetRobotStaticStatus(RobotStaticStatus input){
    RobotStaticStatus_slint status;
    status.connection_state = input.connection_state();
    status.field_state = input.field_state();
    status.alive_state = input.alive_state();
    status.robot_id = input.robot_id();
    status.robot_type = input.robot_type();
    status.performance_system_chassis = input.performance_system_chassis();
    status.performance_system_shooter = input.performance_system_shooter();
    status.level = input.level();
    status.max_health = input.max_health();
    status.max_heat = input.max_heat();
    status.heat_cooldown_rate = input.heat_cooldown_rate();
    status.max_power = input.max_power();
    status.max_buffer_energy = input.max_buffer_energy();
    status.max_chassis_energy = input.max_chassis_energy();
    slint::invoke_from_event_loop([this, status]() {
            factory_ptr_->set_robot_static_status(status);
    });
}


void ComponentManager::SetRobotDynamicStatus(RobotDynamicStatus input){
    RobotDynamicStatus_slint status;
    status.current_health = input.current_health();
    status.current_heat = input.current_heat();
    status.last_projectile_fire_rate = input.last_projectile_fire_rate();
    status.current_chassis_energy = input.current_chassis_energy();
    status.current_buffer_energy = input.current_buffer_energy();
    status.current_experience = input.current_experience();
    status.experience_for_upgrade = input.experience_for_upgrade();
    status.total_projectiles_fired = input.total_projectiles_fired();
    status.remaining_ammo = input.remaining_ammo();
    status.is_out_of_combat = input.is_out_of_combat();
    status.out_of_combat_countdown = input.out_of_combat_countdown();
    status.can_remote_ammo = input.can_remote_ammo();
    status.can_remote_heal = input.can_remote_heal();
    slint::invoke_from_event_loop([this, status]() {
            factory_ptr_->set_robot_dynamic_status(status);
    });
}

void ComponentManager::SetRobotModuleStatus(RobotModuleStatus input) {
    RobotModuleStatus_slint status;
    status.power_manager = input.power_manager();
    status.rfid = input.rfid();
    status.light_strip = input.light_strip();
    status.small_shooter = input.small_shooter();
    status.big_shooter = input.big_shooter();
    status.uwb = input.uwb();
    status.armor = input.armor();
    status.video_transmission = input.video_transmission();
    status.capacitor = input.capacitor();
    status.main_controller = input.main_controller();
    slint::invoke_from_event_loop([this, status]() {
            factory_ptr_->set_robot_module_status(status);
    });



}