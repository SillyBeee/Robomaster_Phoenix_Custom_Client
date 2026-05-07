#include "scheduler_manager.h"

SchedulerManager::SchedulerManager()
{
    is_started_.store(false);
}

SchedulerManager::~SchedulerManager()
{
    Stop();
}

void SchedulerManager::Start(const SchedulerCenterConfig& config)
{
    LOG_WARN("SchedulerManager::Start");
    if (is_started_.load() == true)
    {
        return;
    }
    is_started_.store(true);
    scheduler_config_ = config;
    scheduler_uptr_ = SchedulerFactory::CreateScheduler(scheduler_config_);
    scheduler_uptr_->Start();
}

void SchedulerManager::Stop()
{
    if (is_started_.load() == false)
    {
        return;
    }
    scheduler_uptr_->Stop();
    is_started_.store(false);
}

std::future<void> SchedulerManager::Enqueue(std::function<void()> func, SchedulerTaskPriority priority,
                                            const std::string &name, int processor_id)
{
    return scheduler_uptr_->Enqueue(func, priority, name, processor_id);
}
bool SchedulerManager::RemoveTask(const std::string &name)
{
    return scheduler_uptr_->RemoveTask(name);
}