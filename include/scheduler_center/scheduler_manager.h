#pragma once

#include <atomic>
#include <memory>

#include "singleton.hpp"
#include "logger.hpp"
#include "utils_json_refactor.hpp"
#include "scheduler_base.h"
#include "scheduler_factory.h"

class SchedulerManager
{

    REGISTER_SINGLETON(SchedulerManager)

public:
    SchedulerManager();
    ~SchedulerManager();

    void Start(const SchedulerCenterConfig& config);
    void Stop();
    std::future<void> Enqueue(std::function<void()> func, SchedulerTaskPriority priority = SchedulerTaskPriority::NORMAL,
                                        const std::string &name = "", int processor_id = -1);
    bool RemoveTask(const std::string &name);
private:
    std::unique_ptr<SchedulerBase> scheduler_uptr_; // 调度器
    SchedulerCenterConfig scheduler_config_;   // 相关的配置
    std::atomic_bool is_started_{false};
};
