#ifndef UTILS_JSON_HPP
#define UTILS_JSON_HPP

#include "logger.hpp"
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>
#include <sys/stat.h>
using json = nlohmann::json;


struct ClientConfig
{
    std::vector<int> resolution = { 1920, 1080 };
    std::string port = "/dev/ttyUSB0";
    int baudrate = 115200;

    // 自动生成 to_json 和 from_json
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(ClientConfig, resolution, port, baudrate)
};

struct ComponentConfig
{
    std::string type;
    float rel_x;
    float rel_y;
    float rel_width;
    float rel_height;
    std::string color;
    float opacity;
    uint8_t layer;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(ComponentConfig, type, rel_x, rel_y, rel_width, rel_height, color, opacity, layer)
};


struct ComponentsConfig {
    std::vector<ComponentConfig> components;
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(ComponentsConfig, components)
};

class ClientJsonUtils
{
public:
    // 从文件加载
    bool LoadJsonFromFile(const std::string& file_path)
    {
        try
        {
            std::ifstream file(file_path);
            if (!file.is_open())
            {
                LOG_ERROR("Could not open config file: {}", file_path);
                return false;
            }
            json j;
            file >> j;
            config_ = j.get<ClientConfig>(); // 自动调用 from_json

            struct stat file_info;
            stat(file_path.c_str(), &file_info);
            lastLoadTime = file_info.st_mtime;
            return true;
        }
        catch (const std::exception& e)
        {
            return false;
        }
    }

    // 保存到文件
    bool SaveJsonToFile(const std::string& file_path) const
    {
        try
        {
            json j = config_; // 自动调用 to_json
            std::ofstream file(file_path);
            if (!file.is_open())
            {
                return false;
            }
            file << j.dump(4);
            return true;
        }
        catch (const std::exception& e)
        {
            return false;
        }
    }

    // 检查文件是否被修改
    bool CheckJsonChange(const std::string& file_path)
    {
        struct stat file_info;
        if (stat(file_path.c_str(), &file_info) != 0)
        {
            return false;
        }
        if (lastLoadTime != 0 && file_info.st_mtime <= lastLoadTime)
        {
            return false; // 文件未修改
        }
        return true; // 文件已修改
    }

    ClientConfig& GetConfig()
    {
        return config_;
    }

private:
    ClientConfig config_;
    std::time_t lastLoadTime = 0;
};



class ComponentJsonUtils
{
public:
    // 从文件加载
    bool LoadJsonFromFile(const std::string& file_path)
    {
        try
        {
            std::ifstream file(file_path);
            if (!file.is_open())
            {
                LOG_ERROR("Could not open config file: {}", file_path);
                return false;
            }
            json j;
            file >> j;

            // 解析整个 JSON 对象到 ComponentsConfig 结构
            ComponentsConfig root = j.get<ComponentsConfig>();
            config_ = root.components; // 提取 vector

            struct stat file_info;
            stat(file_path.c_str(), &file_info);
            lastLoadTime = file_info.st_mtime;
            return true;
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("JSON parse error: {}", e.what());
            return false;
        }
    }

    // 保存到文件
    bool SaveJsonToFile(const std::string& file_path) const
    {
        try
        {
            // 构造顶层对象
            ComponentsConfig root;
            root.components = config_;

            json j = root; // 自动调用 to_json
            std::ofstream file(file_path);
            if (!file.is_open())
            {
                return false;
            }
            file << j.dump(4);
            return true;
        }
        catch (const std::exception& e)
        {
            return false;
        }
    }

    // 检查文件是否被修改
    bool CheckJsonChange(const std::string& file_path)
    {
        struct stat file_info;
        if (stat(file_path.c_str(), &file_info) != 0)
        {
            return false;
        }
        if (lastLoadTime != 0 && file_info.st_mtime <= lastLoadTime)
        {
            return false; // 文件未修改
        }
        return true; // 文件已修改
    }

    // 修改返回值类型为 vector
    std::vector<ComponentConfig>& GetConfig()
    {
        return config_;
    }

private:
    // 修改成员变量类型为 vector
    std::vector<ComponentConfig> config_;
    std::time_t lastLoadTime = 0;
};

#endif
