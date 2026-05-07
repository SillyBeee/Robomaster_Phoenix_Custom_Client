#pragma once

#include <queue>
#include <tuple>
#include <unordered_map>

#include "scheduler_base.h"
#include "utils_json_refactor.hpp"

// Classic模式调度器 - 基于优先级的调度
class SchedulerClassic : public SchedulerBase
{
public:
    SchedulerClassic(const SchedulerCenterConfig &config);
    ~SchedulerClassic();

    bool Start() override;
    void Stop() override;
    std::future<void> Enqueue(std::function<void()> func, SchedulerTaskPriority priority = SchedulerTaskPriority::NORMAL,
                                const std::string &name = "", int processor_id = -1) override;
    bool RemoveTask(const std::string &name) override;

private:
    void WorkerThread();

    SchedulerCenterConfig config_;
    std::vector<std::thread> threads_;
    std::atomic<bool> running_{false};

    // 按优先级排序的任务队列
    using TaskQueue = std::priority_queue<std::shared_ptr<SchedulerTask>,
                                            std::vector<std::shared_ptr<SchedulerTask>>,
                                            std::function<bool(const std::shared_ptr<SchedulerTask> &,
                                                                const std::shared_ptr<SchedulerTask> &)>>;

    TaskQueue task_queue_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::unordered_map<std::string, std::shared_ptr<SchedulerTask>> task_map_;
};
