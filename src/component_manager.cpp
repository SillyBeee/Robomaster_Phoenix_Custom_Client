#include "component_manager.hpp"
#include "app-window.h"
#include "logger.hpp"
#include "utils_json.hpp"



void LoadSettings(const Callback_Factory& factory,const std::string& config_path){
    ClientJsonUtils utils;
    if(!utils.LoadJsonFromFile(config_path)){
        LOG_ERROR("Failed to load configuration from {}", config_path);
        return;
    }
    LOG_INFO("config: resolution={}x{}, port={}, baudrate={}",
             utils.GetConfig().resolution[0],
             utils.GetConfig().resolution[1],
             utils.GetConfig().port,
             utils.GetConfig().baudrate);
}

void LoadComponents(const Callback_Factory& factory,std::string config_path){
    auto components_vector = factory.get_components();
    size_t count = components_vector->row_count();
    LOG_INFO("Read {} components from Callback_Factory", count);
    ComponentJsonUtils utils;
    if(!utils.LoadJsonFromFile(config_path)){
        LOG_ERROR("Failed to load components configuration from {}", config_path);
        return; 
    }
    auto& configs = utils.GetConfig();
    LOG_INFO("Loaded {} components from JSON file", configs.size());

    std::vector<ComponentData> slint_components_data;
    slint_components_data.reserve(configs.size());
    for(const auto& config : configs){
        ComponentData data;
        data.type = config.type;
        data.rel_x = config.rel_x;
        data.rel_y = config.rel_y;
        data.rel_width = config.rel_width;
        data.rel_height = config.rel_height;
        data.color = HexToColor(config.color);
        data.opacity = config.opacity;
        data.layer = config.layer;
        slint_components_data.push_back(data);
    }
    auto model = std::make_shared<slint::VectorModel<ComponentData>>(slint_components_data);
    factory.set_components(model);
    LOG_INFO("Loaded and set {} components to UI", slint_components_data.size());
}


void SaveComponents(const Callback_Factory& factory,const  std::string& config_path){
    auto components_vector = factory.get_components();
    size_t count = components_vector->row_count();
    LOG_INFO("Saving {} components to JSON file", count);

    ComponentJsonUtils utils;
    std::vector<ComponentConfig> configs;
    for(size_t i=0;i<count;++i){
        auto data = components_vector->row_data(i);
        ComponentConfig config;
        config.type = data->type;
        config.rel_x = data->rel_x;
        config.rel_y = data->rel_y;
        config.rel_width = data->rel_width;
        config.rel_height = data->rel_height;
        config.color = std::format("#{:02X}{:02X}{:02X}",
                                    static_cast<int>(data->color.color().red()),
                                    static_cast<int>(data->color.color().green()),
                                    static_cast<int>(data->color.color().blue()));
        config.opacity = data->opacity;
        config.layer = data->layer;
        configs.push_back(config);
    }
    utils.SetConfig(configs);
    if(!utils.SaveJsonToFile(config_path)){
        LOG_ERROR("Failed to save components configuration to {}", config_path);
        return; 
    }
    LOG_INFO("Successfully saved {} components to {}", configs.size(), config_path);
}