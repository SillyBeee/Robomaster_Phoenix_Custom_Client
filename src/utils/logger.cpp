#include "logger.hpp"

#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>


namespace logger {

Logger& Logger::GetInstance() {
  static Logger instance;
  return instance;
}

Logger::Logger() : logger_(nullptr) {}

void Logger::Init(const LogLevel _console_level, const LogLevel _file_level,
                  const std::string& _filename, const size_t _max_size,
                  const size_t _max_files) {
  sinks_.clear();

  // 控制台 sink（带颜色）
  auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();

  console_sink->set_level(ConvertLevel(_console_level));
  
  // 修改 pattern：
  // %^ 开始颜色，%$ 结束颜色
  // 将 [%l] (级别) 和 [%s:%#] (文件:行号) 放在颜色范围内
  // 时间和消息内容 %v 保持默认颜色
  console_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] %^[%l] [%s:%#]%$ %v");
  
  sinks_.push_back(console_sink);

  // 可选文件 sink（带滚动）
  if (!_filename.empty()) {
    auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
        _filename, _max_size, _max_files);
    file_sink->set_level(ConvertLevel(_file_level));
    // 文件不需要颜色
    file_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [%s:%#] %v");
    sinks_.push_back(file_sink);
  }

  // 创建 logger 并注册
  logger_ = std::make_shared<spdlog::logger>("hy_common", sinks_.begin(),
                                             sinks_.end());
  // 交由各 sink 负责过滤，因此 logger 级别设置为最小（trace）
  logger_->set_level(spdlog::level::trace);
  spdlog::register_logger(logger_);
}

void Logger::AddSink(spdlog::sink_ptr _sink, const LogLevel _level) {
  if (!_sink) return;
  _sink->set_level(ConvertLevel(_level));
  sinks_.push_back(_sink);
  if (logger_) {
    // 将新的 sink 添加到现有 logger
    logger_->sinks().push_back(_sink);
  }
}

void Logger::SetLevel(const LogLevel _level) {
  if (!logger_) return;
  logger_->set_level(ConvertLevel(_level));
}

std::shared_ptr<spdlog::logger> Logger::GetLogger() {
  if (!logger_) {
    // 如果用户未显式 Init，则用默认参数初始化（控制台 INFO）
    Init();
  }
  return logger_;
}

spdlog::level::level_enum Logger::ConvertLevel(LogLevel _level) {
  switch (_level) {
    case LogLevel::TRACE:
      return spdlog::level::trace;
    case LogLevel::DEBUG:
      return spdlog::level::debug;
    case LogLevel::INFO:
      return spdlog::level::info;
    case LogLevel::WARN:
      return spdlog::level::warn;
    case LogLevel::ERROR:
      return spdlog::level::err;
    case LogLevel::CRITICAL:
      return spdlog::level::critical;
    case LogLevel::OFF:
    default:
      return spdlog::level::off;
  }
}

}  // namespace logger

