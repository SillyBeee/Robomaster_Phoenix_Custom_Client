#include <sched.h>
#include <pthread.h>
#include <cstdint>

#include "scheduler_choreography.h"
#include "utils_json_refactor.hpp"

SchedulerChoreography::SchedulerChoreography(const SchedulerCenterConfig& config) : config_(config) {
    size_t thread_num = config.choreography.choreo_thread_num;
    
    // 初始化队列（普通类型，直接resize）
    choreo_queues_.resize(thread_num);

    choreo_queue_mutexes_.clear();
    for (size_t i = 0; i < thread_num; ++i) {
        choreo_queue_mutexes_.emplace_back(std::make_unique<std::mutex>());
    }

    choreo_queue_cvs_.clear();
    for (size_t i = 0; i < thread_num; ++i) {
        choreo_queue_cvs_.emplace_back(std::make_unique<std::condition_variable>());
    }
    
    // 初始化优先级队列
    pool_queue_ = PoolTaskQueue([](const std::shared_ptr<SchedulerTask>& a, const std::shared_ptr<SchedulerTask>& b) {
        return a->priority_ < b->priority_;  // 优先级高的任务先执行
    });
}


SchedulerChoreography::~SchedulerChoreography() {
    Stop();
}

bool SchedulerChoreography::Start()
{
    if (running_)
        return false;

    running_ = true;

    // 启动choreography线程 特定处理器的线程
    for (size_t i = 0; i < config_.choreography.choreo_thread_num; ++i)
    {
        choreo_threads_.emplace_back(&SchedulerChoreography::ChoreoWorkerThread, this, i);
    }

    // 启动池线程 公共池线程
    for (size_t i = 0; i < config_.choreography.pool_thread_num; ++i)
    {
        pool_threads_.emplace_back(&SchedulerChoreography::PoolWorkerThread, this, i);
    }

    // 应用线程亲和性和调度策略配置
    for (size_t i = 0; i < config_.choreography.thread_affinities.size() && i < choreo_threads_.size(); ++i)
    {
        if (i >= choreo_threads_.size())
        {
            continue;
        }
        SetThreadAffinity(choreo_threads_[i].native_handle(), config_.choreography.thread_affinities[i]);
    }

    // 调度策略和优先级
    for (size_t i = 0; i < config_.choreography.thread_policies.size() && i < choreo_threads_.size(); ++i)
    {
        auto [policy, priority] = config_.choreography.thread_policies[i];
        if (i >= choreo_threads_.size())
        {
            break;
        }

        SetThreadPolicy(choreo_threads_[i].native_handle(), policy, priority);
    }

    return true;
}

void SchedulerChoreography::Stop()
{
    if (!running_)
        return;

    running_ = false;

    for (auto &cv_ptr : choreo_queue_cvs_)
    {
        cv_ptr->notify_all();
    }
    pool_queue_cv_.notify_all();

    // 等待线程结束
    for (auto &thread : choreo_threads_)
    {
        if (thread.joinable())
        {
            thread.join();
        }
    }
    for (auto &thread : pool_threads_)
    {
        if (thread.joinable())
        {
            thread.join();
        }
    }

    choreo_threads_.clear();
    pool_threads_.clear();
    task_map_.clear();
    task_processor_map_.clear();
}

std::future<void> SchedulerChoreography::Enqueue(std::function<void()> func, SchedulerTaskPriority priority,
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

    auto task = std::make_shared<SchedulerTask>(task_func, priority, name, processor_id);

    {
        std::lock_guard<std::mutex> lock(pool_queue_mutex_);
        if (!name.empty())
        {
            task_map_[name] = task;
        }

        // 如果指定了有效的处理器ID，使用choreography队列
        if (processor_id >= 0 && static_cast<size_t>(processor_id) < config_.choreography.choreo_thread_num)
        {
            std::lock_guard<std::mutex> choreo_lock(*(choreo_queue_mutexes_[processor_id]));
            choreo_queues_[processor_id].push(task);
            task_processor_map_[name] = processor_id;
            choreo_queue_cvs_[processor_id]->notify_one();
        }
        else
        {
            // 否则使用公共池队列
            pool_queue_.push(task);
            pool_queue_cv_.notify_one();
        }
    }

    return future;
}

bool SchedulerChoreography::RemoveTask(const std::string &name)
{
    if (name.empty())
        return false;

    std::lock_guard<std::mutex> lock(pool_queue_mutex_);
    auto it = task_map_.find(name);
    if (it == task_map_.end())
        return false;

    // 标记任务为取消
    it->second->cancelled_ = true;

    // 如果是choreography任务，从对应队列中移除
    auto proc_it = task_processor_map_.find(name);
    if (proc_it != task_processor_map_.end())
    {
        int proc_id = proc_it->second;
        std::lock_guard<std::mutex> choreo_lock(*(choreo_queue_mutexes_[proc_id]));

        // 创建新队列，过滤掉要移除的任务
        std::queue<std::shared_ptr<SchedulerTask>> new_queue;
        while (!choreo_queues_[proc_id].empty())
        {
            auto task = choreo_queues_[proc_id].front();
            choreo_queues_[proc_id].pop();

            if (task->name_ != name)
            {
                new_queue.push(task);
            }
        }
        choreo_queues_[proc_id] = std::move(new_queue);
        task_processor_map_.erase(proc_it);
    }

    task_map_.erase(it);
    return true;
}

void SchedulerChoreography::ChoreoWorkerThread(int processor_id)
{
    while (running_)
    {
        std::shared_ptr<SchedulerTask> task;

        {
            std::unique_lock<std::mutex> lock(*(choreo_queue_mutexes_[processor_id]));
            choreo_queue_cvs_[processor_id]->wait(lock, [this, processor_id]()
                                                    { return !running_ || !choreo_queues_[processor_id].empty(); });

            if (!running_)
                break;
            if (choreo_queues_[processor_id].empty())
                continue;

            task = choreo_queues_[processor_id].front();
            choreo_queues_[processor_id].pop();
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
                LOG_ERROR("Choreo task execution error: {}", e.what());
                return;
            }

            // 从映射中移除已完成的任务
            if (!task->name_.empty())
            {
                std::lock_guard<std::mutex> lock(pool_queue_mutex_);
                task_map_.erase(task->name_);
                task_processor_map_.erase(task->name_);
            }
        }
    }
}

void SchedulerChoreography::PoolWorkerThread(int processor_id)
{
    while (running_)
    {
        std::shared_ptr<SchedulerTask> task;

        {
            std::unique_lock<std::mutex> lock(pool_queue_mutex_);
            pool_queue_cv_.wait(lock, [this]()
                                { return !running_ || !pool_queue_.empty(); });

            if (!running_)
                break;
            if (pool_queue_.empty())
                continue;

            task = pool_queue_.top();
            pool_queue_.pop();

            // 从映射中移除
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
                LOG_ERROR("Pool task execution error: {}", e.what());
                return;
            }
        }
    }
}

bool SchedulerChoreography::SetThreadAffinity(pthread_t thread, const std::vector<int> &cpus)
{
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);

    for (int cpu : cpus)
    {
        CPU_SET(cpu, &cpuset);
    }

    int rc = pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset);
    if (rc != 0)
    {
        LOG_ERROR("Error setting thread affinity: {}", rc);
        return false;
    }
    return true;
}

bool SchedulerChoreography::SetThreadPolicy(pthread_t thread, const int policy, const int priority)
{

    // SCHED_OTHER,  // 标准轮询调度,非实时,低优先级，会被实时线程抢占
    // SCHED_FIFO,   // 先进先出实时调度, 高优先级线程会一直运行,直到主动放弃
    // SCHED_RR      // 时间片轮转实时调度，需兼顾实时性和公平性的实时任务

    // 校验调度策略的合法性
    if (policy != SCHED_OTHER && policy != SCHED_FIFO && policy != SCHED_RR) {
        LOG_ERROR("Invalid thread policy: {}", policy);
        return false;
    }

    struct sched_param param;
    // 根据策略校验并设置优先级
    if (policy == SCHED_OTHER) {
        // 非实时策略，优先级必须设为0
        param.sched_priority = 0;
        if (priority != 0) {
            LOG_WARN("SCHED_OTHER ignores priority, force set to 0");
        }
    } else {
        // 实时策略，校验优先级范围（Linux默认1~99）
        int min_prio = sched_get_priority_min(policy);
        int max_prio = sched_get_priority_max(policy);
        if (priority < min_prio || priority > max_prio) {
            LOG_ERROR("Priority {} out of range [{}, {}] for policy {}", 
                      priority, min_prio, max_prio, policy);
            return false;
        }
        param.sched_priority = priority;
    }

    int rc = pthread_setschedparam(thread, policy, &param);
    if (rc != 0)
    {
        LOG_WARN("Failed to set thread policy (EPERM: need root for RT), fallback to SCHED_OTHER: {}", rc);
        param.sched_priority = 0;
        pthread_setschedparam(thread, SCHED_OTHER, &param);
        return false;
    }
    return true;
}
