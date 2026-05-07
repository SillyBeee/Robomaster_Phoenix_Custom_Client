
#pragma once


#include "scheduler_classic.h"
#include "scheduler_choreography.h"

// 调度器工厂
class SchedulerFactory {
public:
    static std::unique_ptr<SchedulerBase> CreateScheduler(const SchedulerCenterConfig& config) {
        switch (config.type) {
            case SchedulerCenterConfig::Type::CLASSIC:
                return std::make_unique<SchedulerClassic>(config);
            case SchedulerCenterConfig::Type::CHOREOGRAPHY:
                return std::make_unique<SchedulerChoreography>(config);
            default:
                return nullptr;
        }
    }
};