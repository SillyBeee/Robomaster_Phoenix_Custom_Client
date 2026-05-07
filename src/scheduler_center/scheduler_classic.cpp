
#include <sched.h>
#include <pthread.h>
#include <cstdint>
#include <sstream>

#include "scheduler_classic.h"

// SchedulerClassic 实现
SchedulerClassic::SchedulerClassic(const SchedulerCenterConfig &config) : config_(config)
{
    // 初始化优先级队列，优先级高的任务先执行
    task_queue_ = TaskQueue([](const std::shared_ptr<SchedulerTask> &a, const std::shared_ptr<SchedulerTask> &b)
                            { return a->priority_ < b->priority_; });
}

SchedulerClassic::~SchedulerClassic()
{
    Stop();
}

bool SchedulerClassic::Start()
{
    if (running_)
        return false;

    running_ = true;
    for (size_t i = 0; i < config_.thread_num; ++i)
    {
        threads_.emplace_back(&SchedulerClassic::WorkerThread, this);
    }
    return true;
}

void SchedulerClassic::Stop()
{
    if (!running_)
        return;

    running_ = false;
    queue_cv_.notify_all();

    for (auto &thread : threads_)
    {
        if (thread.joinable())
        {
            thread.join();
        }
    }
    threads_.clear();

    // 清空任务队列
    std::lock_guard<std::mutex> lock(queue_mutex_);
    while (!task_queue_.empty())
    {
        task_queue_.pop();
    }
    task_map_.clear();
}

std::future<void> SchedulerClassic::Enqueue(std::function<void()> func, SchedulerTaskPriority priority,
                                            const std::string &name, int processor_id)
{
    if (!running_)
    {
        LOG_ERROR("Scheduler is not running");
        return std::future<void>();
    }

    // 创建包装任务以支持future
    auto promise = std::make_shared<std::promise<void>>();
    auto future = promise->get_future();

    auto task_func = [func = std::move(func), promise]()
    {
        try
        {
            func();
            promise->set_value();
        }
        catch (...)
        {
            promise->set_exception(std::current_exception());
        }
    };

    auto task = std::make_shared<SchedulerTask>(task_func, priority, name);

    std::lock_guard<std::mutex> lock(queue_mutex_);
    if (!name.empty())
    {
        task_map_[name] = task;
    }
    task_queue_.push(task);
    queue_cv_.notify_one();

    return future;
}

bool SchedulerClassic::RemoveTask(const std::string &name)
{
    if (name.empty())
        return false;

    std::lock_guard<std::mutex> lock(queue_mutex_);
    auto it = task_map_.find(name);
    if (it == task_map_.end())
        return false;

    // 标记任务为取消
    it->second->cancelled_ = true;
    task_map_.erase(it);
    return true;
}

void SchedulerClassic::WorkerThread()
{
    while (running_)
    {
        std::shared_ptr<SchedulerTask> task;

        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_cv_.wait(lock, [this]()
                            { return !running_ || !task_queue_.empty(); });

            if (!running_)
                break;
            if (task_queue_.empty())
                continue;

            task = task_queue_.top();
            task_queue_.pop();

            // 移除已取消的任务
            if (!task->name_.empty())
            {
                task_map_.erase(task->name_);
            }
        }

        // 执行未取消的任务
        if (task && !task->cancelled_)
        {
            try
            {
                task->func_();
            }
            catch (const std::exception &e)
            {
                LOG_ERROR("SchedulerTask execution error: {}", e.what());
                return;
            }
        }
    }
}
