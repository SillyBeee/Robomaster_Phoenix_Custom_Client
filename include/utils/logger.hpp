#pragma once

#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <memory>
#include <string>




namespace logger {

/**
 * @brief 日志级别枚举
 */
enum class LogLevel { TRACE, DEBUG, INFO, WARN, ERROR, CRITICAL, OFF };

/**
 * @brief 日志管理类
 */
class Logger {
 public:
  /**
   * @brief 获取日志单例实例
   * @return Logger& 日志实例引用
   */
  static Logger& GetInstance();

  /**
   * @brief 初始化日志系统
   * @param _console_level 控制台日志级别
   * @param _file_level 文件日志级别
   * @param _filename 日志文件名，默认为空（不记录到文件）
   * @param _max_size 单个日志文件最大大小（字节），默认为 5MB
   * @param _max_files 最大日志文件数量，默认为 3
   */
  void Init(const LogLevel _console_level = LogLevel::INFO,
            const LogLevel _file_level = LogLevel::DEBUG,
            const std::string& _filename = "",
            const size_t _max_size = 5 * 1024 * 1024,
            const size_t _max_files = 3);

  /**
   * @brief 添加日志接收器
   * @param _sink 日志接收器
   * @param _level 日志级别，默认为 INFO
   */
  void AddSink(spdlog::sink_ptr _sink, const LogLevel _level = LogLevel::INFO);

  /**
   * @brief 设置日志级别
   * @param _level 日志级别
   */
  void SetLevel(const LogLevel _level);

  /**
   * @brief 获取日志记录器
   * @return std::shared_ptr<spdlog::logger> 日志记录器
   */
  std::shared_ptr<spdlog::logger> GetLogger();

 private:
  Logger();
  ~Logger() = default;
  Logger(const Logger&) = delete;
  Logger& operator=(const Logger&) = delete;

  /**
   * @brief 转换日志级别
   * @param _level 自定义日志级别
   * @return spdlog::level::level_enum spdlog日志级别
   */
  spdlog::level::level_enum ConvertLevel(LogLevel _level);

 private:
  /// @brief 日志接收器列表
  std::vector<spdlog::sink_ptr> sinks_;

  /// @brief 日志记录器
  std::shared_ptr<spdlog::logger> logger_;
};

#define LOG_TRACE(...)                                                      \
  SPDLOG_LOGGER_TRACE(logger::Logger::GetInstance().GetLogger(), \
                      __VA_ARGS__)
#define LOG_DEBUG(...)                                                      \
  SPDLOG_LOGGER_DEBUG(logger::Logger::GetInstance().GetLogger(), \
                      __VA_ARGS__)
#define LOG_INFO(...)                                                      \
  SPDLOG_LOGGER_INFO(logger::Logger::GetInstance().GetLogger(), \
                     __VA_ARGS__)
#define LOG_WARN(...)                                                      \
  SPDLOG_LOGGER_WARN(logger::Logger::GetInstance().GetLogger(), \
                     __VA_ARGS__)
#define LOG_ERROR(...)                                                      \
  SPDLOG_LOGGER_ERROR(logger::Logger::GetInstance().GetLogger(), \
                      __VA_ARGS__)
#define LOG_CRITICAL(...)                                                      \
  SPDLOG_LOGGER_CRITICAL(logger::Logger::GetInstance().GetLogger(), \
                         __VA_ARGS__)

}  // namespace logger

