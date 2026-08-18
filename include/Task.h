#pragma once

#include <string>
#include <chrono>
#include <functional>
#include <atomic>
#include <memory>
#include "Concurrency.h"

enum class TaskPriority {
    LOW = 0,
    MEDIUM = 1,
    HIGH = 2
};

enum class TaskStatus {
    PENDING,
    RUNNING,
    COMPLETED,
    FAILED,
    CANCELLED
};

// String helper functions for enum outputs
std::string priorityToString(TaskPriority priority);
std::string statusToString(TaskStatus status);

class Task {
public:
    using TaskId = uint32_t;
    // TaskWork returns true if successful, false if failed.
    using TaskWork = std::function<bool()>;

    Task(TaskId id, std::string name, TaskPriority priority, 
         std::chrono::milliseconds duration, int max_retries = 0, 
         TaskWork work = nullptr);

    // Disable copy to enforce unique ownership semantics via smart pointers
    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;

    // Thread-safe accessors and status management
    TaskId getId() const { return id_; }
    std::string getName() const { return name_; }
    TaskPriority getPriority() const { return priority_; }
    
    TaskStatus getStatus();
    void setStatus(TaskStatus status);

    std::chrono::system_clock::time_point getCreationTime() const { return creation_time_; }
    std::chrono::system_clock::time_point getStartTime();
    std::chrono::system_clock::time_point getCompletionTime();
    std::chrono::milliseconds getDuration() const { return duration_; }
    
    std::chrono::system_clock::time_point getExecuteAfter() const;
    void setExecuteAfter(std::chrono::system_clock::time_point execute_after);

    int getRetryCount();
    int getMaxRetries() const { return max_retries_; }
    void incrementRetryCount();

    // Cooperative cancellation interface
    void cancel();
    bool isCancelled();

    // Runs the task work. Handles simulated work or custom work callback.
    // Returns true if successful, false otherwise.
    bool execute();

private:
    const TaskId id_;
    const std::string name_;
    const TaskPriority priority_;
    const std::chrono::milliseconds duration_;
    const int max_retries_;
    const TaskWork work_;
    const std::chrono::system_clock::time_point creation_time_;

    mutable Mutex mutex_;
    TaskStatus status_{TaskStatus::PENDING};
    int retry_count_{0};
    std::chrono::system_clock::time_point start_time_;
    std::chrono::system_clock::time_point completion_time_;
    std::chrono::system_clock::time_point execute_after_; // Set for delayed tasks
    std::atomic<bool> cancelled_{false};
};
