#pragma once

#include <deque>
#include <map>
#include <queue>
#include <unordered_map>
#include <memory> // 新增：智能指针依赖头文件

#include "scheduler_base.h"
#include "utils_json_refactor.hpp"

// Choreography模式调度器 - 任务绑定特定处理器
class SchedulerChoreography : public SchedulerBase
{
public:
    SchedulerChoreography(const SchedulerCenterConfig &config);
    ~SchedulerChoreography();

    bool Start() override;
    void Stop() override;
    std::future<void> Enqueue(std::function<void()> func, SchedulerTaskPriority priority = SchedulerTaskPriority::NORMAL,
                                const std::string &name = "", int processor_id = -1) override;
    bool RemoveTask(const std::string &name) override;

private:
    void ChoreoWorkerThread(int processor_id);
    void PoolWorkerThread(int processor_id);
    // 设置线程亲和性
    bool SetThreadAffinity(pthread_t thread, const std::vector<int> &cpus);
    // 设置线程调度策略
    bool SetThreadPolicy(pthread_t thread, const int policy, const int priority);

    SchedulerCenterConfig config_;
    std::vector<std::thread> choreo_threads_;
    std::vector<std::thread> pool_threads_;
    std::atomic<bool> running_{false};

    // 每个处理器有自己的任务队列
    std::vector<std::queue<std::shared_ptr<SchedulerTask>>> choreo_queues_;
    // 用std::unique_ptr包装不可移动类型
    std::vector<std::unique_ptr<std::mutex>> choreo_queue_mutexes_;
    std::vector<std::unique_ptr<std::condition_variable>> choreo_queue_cvs_;

    // 优先级队列用于公共池
    using PoolTaskQueue = std::priority_queue<std::shared_ptr<SchedulerTask>, std::vector<std::shared_ptr<SchedulerTask>>,
                                                std::function<bool(const std::shared_ptr<SchedulerTask> &, const std::shared_ptr<SchedulerTask> &)>>;

    PoolTaskQueue pool_queue_;
    std::mutex pool_queue_mutex_;
    std::condition_variable pool_queue_cv_;

    std::unordered_map<std::string, std::shared_ptr<SchedulerTask>> task_map_;
    std::unordered_map<std::string, int> task_processor_map_; // 任务到处理器的映射
};
