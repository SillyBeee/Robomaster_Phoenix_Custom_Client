#pragma once

#include <future>
#include <thread>
#include <condition_variable>
#include <atomic>
#include <atomic>

#include "logger.hpp"
#include "scheduler_define.h"


// 调度器基类
class SchedulerBase
{
public:
    virtual ~SchedulerBase() = default;

    virtual bool Start() = 0;
    virtual void Stop() = 0;
    virtual std::future<void> Enqueue(std::function<void()> func, SchedulerTaskPriority priority = SchedulerTaskPriority::NORMAL,
                                        const std::string &name = "", int processor_id = -1) = 0;
    virtual bool RemoveTask(const std::string &name) = 0;
};
