#pragma once

#include <vector>
#include <functional>
#include <string>
#include <atomic>
#include <memory>

// 任务优先级
enum class SchedulerTaskPriority
{
    LOW = 0,
    NORMAL = 1,
    HIGH = 2,
    REALTIME = 3
};

// 任务结构
struct SchedulerTask
{
    SchedulerTask(std::function<void()> func, SchedulerTaskPriority priority, const std::string &name, int processor_id = -1)
        : func_(std::move(func)), priority_(priority), name_(name), processor_id_(processor_id) {}

    std::function<void()> func_;
    SchedulerTaskPriority priority_;
    std::string name_;
    int processor_id_; // 用于choreography模式，指定处理器ID
    std::atomic<bool> cancelled_{false};
};
