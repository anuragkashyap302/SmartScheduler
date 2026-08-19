#pragma once

#include "Task.h"
#include "TaskQueue.h"
#include "ThreadPool.h"
#include "Statistics.h"
#include <unordered_map>
#include <vector>
#include "Concurrency.h"

struct DelayedTask {
    std::shared_ptr<Task> task;
    std::chrono::system_clock::time_point execute_after;

    // Sort such that the earliest execute_after is on top of the min-heap
    bool operator<(const DelayedTask& other) const {
        return execute_after > other.execute_after;
    }
};

struct StatsSnapshot {
    uint32_t total_submitted;
    uint32_t pending_tasks;
    uint32_t running_tasks;
    uint32_t completed_tasks;
    uint32_t failed_tasks;
    uint32_t cancelled_tasks;
    double average_execution_time_sec;
    uint32_t active_workers;
    size_t current_queue_size;
};

class Scheduler {
public:
    Scheduler(size_t num_workers);
    ~Scheduler();

    // Disable copy/assignment
    Scheduler(const Scheduler&) = delete;
    Scheduler& operator=(const Scheduler&) = delete;

    // Starts the worker threads and the delayed task manager
    void start();

    // Gracefully shuts down the scheduler (stops delayed manager and worker threads)
    void shutdown();

    // Submits a task for immediate execution
    std::shared_ptr<Task> submitTask(const std::string& name, TaskPriority priority, 
                                     std::chrono::milliseconds duration, int max_retries = 0,
                                     Task::TaskWork work = nullptr);

    // Submits a task with a delay
    std::shared_ptr<Task> submitTaskDelayed(const std::string& name, TaskPriority priority, 
                                            std::chrono::milliseconds duration, 
                                            std::chrono::milliseconds delay, int max_retries = 0,
                                            Task::TaskWork work = nullptr);

    // Cancels a pending or running task by ID
    bool cancelTask(Task::TaskId id);

    // Registry accessors
    std::shared_ptr<Task> getTask(Task::TaskId id);
    std::vector<std::shared_ptr<Task>> getAllTasks();

    // Returns a copyable snapshot of current statistics
    StatsSnapshot getStats() const;

    // Prints the formatted statistics to console
    void printStats();

    // Internal methods used by ThreadPool workers
    std::shared_ptr<Task> popTask();
    void runTask(std::shared_ptr<Task> task, size_t worker_id);

private:
    void delayedTaskWorkerLoop();
    void requeueTask(std::shared_ptr<Task> task);

    std::atomic<Task::TaskId> next_task_id_{1};
    mutable Statistics stats_;
    TaskQueue taskQueue_;
    ThreadPool threadPool_;

    // Registry of all tasks (active, completed, cancelled, failed)
    mutable Mutex registry_mutex_;
    std::unordered_map<Task::TaskId, std::shared_ptr<Task>> registry_;

    // Delayed Task Min-Heap
    std::priority_queue<DelayedTask> delayed_queue_;
    Mutex delayed_mutex_;
    ConditionVariable delayed_cv_;
    Thread delayed_worker_thread_;
    
    std::atomic<bool> delayed_shutdown_{false};
    std::atomic<bool> scheduler_shutdown_{false};
    bool started_{false};
};
