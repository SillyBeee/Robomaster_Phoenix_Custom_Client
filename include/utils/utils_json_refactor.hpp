#ifndef UTILS_JSON_REFFACTOR_HPP
#define UTILS_JSON_REFFACTOR_HPP

#include "logger.hpp"
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>
#include <sys/stat.h>

using json = nlohmann::json;

struct ClientConfig
{
    std::vector<int> resolution = { 1920, 1080 };
    std::string decode_test_mode = "full_pipeline";
    // 兼容 repeat_headers/intra-refresh 不同策略：
    // true/true = 严格模式；true/false = 兼容无 IDR 关键帧流
    bool h264_wait_for_sps_pps = true;
    bool h264_wait_for_idr = true;
    std::string urdf_path = "urdf_renderer/arm_description/urdf/Engineer2.urdf";
    std::string default_mqtt_client_id = "3";

    // 自动生成 to_json 和 from_json
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(ClientConfig, resolution,
                                   decode_test_mode, h264_wait_for_sps_pps,
                                   h264_wait_for_idr, urdf_path,
                                   default_mqtt_client_id)
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

struct ComponentsConfig
{
    std::vector<ComponentConfig> components;
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(ComponentsConfig, components)
};

// JSON serialization for std::pair<int,int>
namespace nlohmann {
template<>
struct adl_serializer<std::pair<int, int>> {
    static void to_json(json& j, const std::pair<int, int>& p) {
        j = json::array({p.first, p.second});
    }
    static void from_json(const json& j, std::pair<int, int>& p) {
        j.at(0).get_to(p.first);
        j.at(1).get_to(p.second);
    }
};
}

// 调度器配置结构
struct SchedulerCenterConfig
{
    enum class Type
    {
        CLASSIC,
        CHOREOGRAPHY
    };

    Type type{Type::CLASSIC};
    int thread_num{1};

    // Choreography模式特有配置
    struct ChoreographyConfig
    {
        int choreo_thread_num;   // 绑定到特定处理器的线程数
        int pool_thread_num; // 公共池线程数
        std::vector<std::vector<int>> thread_affinities;    // 绑核亲和度
        std::vector<std::pair<int, int>> thread_policies; // 第一个是策略，第二个是优先级

        NLOHMANN_DEFINE_TYPE_INTRUSIVE(ChoreographyConfig, choreo_thread_num, pool_thread_num, thread_affinities, thread_policies)
    };

    ChoreographyConfig choreography;

    void reset(){
        type = Type::CLASSIC;
        thread_num = 1;
    }

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(SchedulerCenterConfig, type, thread_num, choreography)
};

NLOHMANN_JSON_SERIALIZE_ENUM(SchedulerCenterConfig::Type, {
    { SchedulerCenterConfig::Type::CLASSIC, "CLASSIC" },
    { SchedulerCenterConfig::Type::CHOREOGRAPHY, "CHOREOGRAPHY" },
})



template<typename TConfig>
class JsonUtils
{
    static_assert(
        nlohmann::detail::is_compatible_type<nlohmann::json, TConfig>::value,
        "JsonUtils<TConfig>: TConfig must be compatible with nlohmann::json "
        "(define to_json/from_json, e.g. via NLOHMANN_DEFINE_TYPE_INTRUSIVE/NON_INTRUSIVE)."
    );

public:
    /**
    * @brief 设置当前配置文件存储目录
    * 
    * @param dir_path 
    * @return true 
    * @return false 
    */
    bool SetConfigDirPath(const std::string& dir_path)
    {
        this->config_dir_path_ = std::filesystem::path(dir_path);
        if(!std::filesystem::exists(config_dir_path_))
        {
            return false;
        }
        return true;
    }

    /**
     * @brief 获取配置文件列表
     * 
     * @return std::vector<std::string> 
     */
    std::vector<std::string> GetConfigFileList()
    {
        std::vector<std::string> file_list;
        if (config_dir_path_.empty())
        {
            LOG_ERROR("Config directory path is empty.");
            return file_list;
        }
        if (!std::filesystem::exists(config_dir_path_) || !std::filesystem::is_directory(config_dir_path_))
        {
            LOG_ERROR("Config directory does not exist or is not a directory: {}", config_dir_path_.string());
            return file_list;
        }
        for (const auto& entry: std::filesystem::directory_iterator(config_dir_path_))
        {
            if (entry.is_regular_file() && entry.path().extension() == ".json")
            {
                file_list.push_back(entry.path().filename().string());
            }
        }
        return file_list;
    }

    /**
     * @brief 设置当前配置文件
     * 
     * @param file_name 
     * @return true 
     * @return false 
     */
    bool SetCurrentConfigFile(const std::string& file_name)
    {
        if (config_dir_path_.empty())
        {
            LOG_ERROR("Config directory path is empty.");
            return false;
        }
        std::filesystem::path file_path = config_dir_path_ / file_name;
        if (!std::filesystem::exists(file_path) || !std::filesystem::is_regular_file(file_path))
        {
            LOG_ERROR("Config file does not exist: {}", file_path.string());
            return false;
        }
        current_config_file_ = file_path;
        return true;
    }

    /**
     * @brief 从文件加载 JSON 配置,如果传入string为空,默认加载当前类选中的配置文件
     * 
     * @param file_path 
     * @return true 
     * @return false 
     */
    bool LoadJsonFromFile(const std::string& file_path = "")
    {
        std::filesystem::path path_to_load;
        path_to_load = file_path.empty() ? current_config_file_ : std::filesystem::path(file_path);
        if (path_to_load.empty())
        {
            LOG_ERROR("No configuration file specified to load.");
            return false;
        }
        try
        {
            std::ifstream file(path_to_load);
            if (!file.is_open())
            {
                LOG_ERROR("Failed to open configuration file: {}", path_to_load.string());
                return false;
            }
            json j;
            file >> j;
            config_ = j.get<TConfig>();

            struct stat file_info;
            stat(path_to_load.c_str(), &file_info);
            lastLoadTime = file_info.st_mtime;
            return true;
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("JSON parse error: {}", e.what());
            return false;
        }
    }

    /**
     * @brief 将当前配置保存到文件,如果传入string为空,默认保存到当前类选中的配置文件
     * 
     * @param file_path 
     * @return true 
     * @return false 
     */
    bool SaveJsonToFile(const std::string& file_path)
    {
        std::filesystem::path path_to_load;
        path_to_load = file_path.empty() ? current_config_file_ : std::filesystem::path(file_path);
        if (path_to_load.empty())
        {
            LOG_ERROR("No configuration file specified to load.");
            return false;
        }
        try
        {
            json j = config_;
            std::ofstream file(path_to_load);
            if (!file.is_open())
            {
                LOG_ERROR("Failed to open file for writing: {}", path_to_load.string());
                return false;
            }
            file << j.dump(4);
            return true;
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("Failed to save JSON to file: {}", e.what());
            return false;
        }
    }

    /**
     * @brief 获取当前配置对象的结构体
     * 
     * @return TConfig& 
     */
    TConfig& GetConfig()
    {
        return config_;
    }

    /**
     * @brief 设置当前配置对象的结构体
     * 
     * @param config 
     */
    void SetConfig(TConfig config)
    {
        config_ = config;
    }

private:
    TConfig config_;
    std::filesystem::path config_dir_path_;
    std::filesystem::path current_config_file_;
    std::time_t lastLoadTime = 0;
};

#endif