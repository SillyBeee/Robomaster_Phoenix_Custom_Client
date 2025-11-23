#ifndef UTILS_JSON_HPP
#define UTILS_JSON_HPP

#include <nlohmann/json.hpp>
#include <fstream>
#include <string>
#include <sys/stat.h>

using json = nlohmann::json;

// 定义一个配置结构
struct ClientConfig {
    std::string resolution = "1920x1080";
    std::string port = "/dev/ttyUSB0";
    int baudrate = 115200;
    
    // 自动生成 to_json 和 from_json
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(ClientConfig, 
                                   resolution, port, baudrate)
};


class JsonUtils {
public:
    // 从文件加载
    bool load_json_from_file(const std::string& file_path) {
        try {
            std::ifstream file(file_path);
            if (!file.is_open()) {
                return false;
            }
            json j;
            file >> j;
            config_ = j.get<ClientConfig>();  // 自动调用 from_json
            
            struct stat fileInfo;
            stat(file_path.c_str(), &fileInfo);
            lastLoadTime = fileInfo.st_mtime;
            return true;
        } catch (const std::exception& e) {
            return false;
        }
    }

    // 保存到文件
    bool save_json_to_file(const std::string& file_path) const {
        try {
            json j = config_;  // 自动调用 to_json
            std::ofstream file(file_path);
            if (!file.is_open()) {
                return false;
            }
            file << j.dump(4);
            return true;
        } catch (const std::exception& e) {
            return false;
        }
    }

    // 检查文件是否被修改
    bool check_json_change(const std::string& file_path) {
        struct stat fileInfo;
        if (stat(file_path.c_str(), &fileInfo) != 0) {
            return false;
        }
        if (lastLoadTime != 0 && fileInfo.st_mtime <= lastLoadTime) {
            return false;  // 文件未修改
        }
        return true;  // 文件已修改
    }

    ClientConfig& get_config() {
        return config_;
    }

private:
    ClientConfig config_;
    std::time_t lastLoadTime = 0;
};

#endif
