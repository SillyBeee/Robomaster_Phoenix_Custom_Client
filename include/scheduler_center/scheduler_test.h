
#include "nlohmann/json.hpp"
#include <iostream>
#include <chrono>
#include <thread>
#include <atomic>
#include <iomanip>

#include "scheduler_manager.h"

using json = nlohmann::json;

void NonMemberSchedulerCpu(const std::string& name)
{
    int cpu_core = sched_getcpu();
    LOG_INFO("name: {}, current CPU core: {}", name, cpu_core);
}

class SchedulerTest {
public:
    static void test_task(const std::string& name, int sleep_ms) {
        LOG_INFO("Task {} started (sleep {}ms)", name, sleep_ms);
        std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
        LOG_INFO("Task {} completed", name);
    }

    void test_classic_scheduler(const SchedulerCenterConfig& config) {
        LOG_INFO("=== Testing Classic Scheduler ===");

        auto& scheduler = SchedulerManager::GetInstance();
        scheduler.Start(config);

        auto high_prio_future = scheduler.Enqueue(
            []() { test_task("High Priority", 100); },
            SchedulerTaskPriority::HIGH, "high_prio_task");

        auto normal1_future = scheduler.Enqueue(
            []() { test_task("Normal 1", 200); },
            SchedulerTaskPriority::NORMAL, "normal1_task");

        auto low_future = scheduler.Enqueue(
            []() { test_task("Low Priority", 150); },
            SchedulerTaskPriority::LOW, "low_prio_task");

        auto normal2_future = scheduler.Enqueue(
            []() { test_task("Normal 2", 50);},
            SchedulerTaskPriority::NORMAL, "normal2_task");

        high_prio_future.get();
        normal1_future.get();
        low_future.get();
        normal2_future.get();

        scheduler.Stop();
    }

    void test_choreography_scheduler(const SchedulerCenterConfig& config) {
        LOG_INFO("=== Testing Choreography Scheduler ===");

        auto& scheduler = SchedulerManager::GetInstance();
        scheduler.Start(config);

        auto choreo1_future = scheduler.Enqueue(
            []() { test_task("Choreo 1 (CPU 0)", 200); },
            SchedulerTaskPriority::NORMAL, "choreo1_task", 0);

        auto choreo2_future = scheduler.Enqueue(
            []() { test_task("Choreo 2 (CPU 1)", 150); },
            SchedulerTaskPriority::NORMAL, "choreo2_task", 1);

        auto pool1_future = scheduler.Enqueue(
            []() { test_task("Pool 1", 100); },
            SchedulerTaskPriority::HIGH, "pool1_task");

        auto pool2_future = scheduler.Enqueue(
            []() { test_task("Pool 2", 50); },
            SchedulerTaskPriority::NORMAL, "pool2_task");

        choreo1_future.get();
        choreo2_future.get();
        pool1_future.get();
        pool2_future.get();

        scheduler.Stop();
    }

    void test_task_cancellation(const SchedulerCenterConfig& config) {
        LOG_INFO("=== Testing Task Cancellation ===");

        auto& scheduler = SchedulerManager::GetInstance();
        scheduler.Start(config);

        std::atomic<bool> task_executed{false};

        auto long_task_future = scheduler.Enqueue(
            [&task_executed]() {
                task_executed = true;
                LOG_INFO("Long task start");
                std::this_thread::sleep_for(std::chrono::seconds(2));
                LOG_INFO("Long task completed (should not see this if cancelled)");
            },
            SchedulerTaskPriority::NORMAL, "long_running_task");

        auto cancel_task_future = scheduler.Enqueue(
            [&scheduler]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                bool cancelled = scheduler.RemoveTask("long_running_task");
                LOG_INFO("Cancellation attempt: {}", (cancelled ? "success" : "failed"));
            },
            SchedulerTaskPriority::HIGH);

        cancel_task_future.get();

        LOG_INFO("Long task started: {}", (task_executed ? "yes" : "no"));

        scheduler.Stop();
    }

    void testSchedCpu(const SchedulerCenterConfig& config) {
        LOG_INFO("=== Testing CPU Affinity ===");

        auto& scheduler = SchedulerManager::GetInstance();
        scheduler.Start(config);

        auto future1 = scheduler.Enqueue(
            []() { NonMemberSchedulerCpu("task_a"); },
            SchedulerTaskPriority::NORMAL, "task_a", 0);

        auto future2 = scheduler.Enqueue(
            []() { NonMemberSchedulerCpu("task_b"); },
            SchedulerTaskPriority::NORMAL, "task_b", 1);

        future1.get();
        future2.get();

        scheduler.Stop();

        LOG_INFO("=== CPU Affinity Test End ===");
    }

    void run_all_tests(const SchedulerCenterConfig& config) {
        testSchedCpu(config);
    }
};
