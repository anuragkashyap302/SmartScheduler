#include "Task.h"
#include "Concurrency.h"
#include <exception>

std::string priorityToString(TaskPriority priority) {
    switch (priority) {
        case TaskPriority::LOW:    return "LOW";
        case TaskPriority::MEDIUM: return "MEDIUM";
        case TaskPriority::HIGH:   return "HIGH";
        default:                   return "UNKNOWN";
    }
}

std::string statusToString(TaskStatus status) {
    switch (status) {
        case TaskStatus::PENDING:   return "PENDING";
        case TaskStatus::RUNNING:   return "RUNNING";
        case TaskStatus::COMPLETED: return "COMPLETED";
        case TaskStatus::FAILED:    return "FAILED";
        case TaskStatus::CANCELLED: return "CANCELLED";
        default:                    return "UNKNOWN";
    }
}

Task::Task(TaskId id, std::string name, TaskPriority priority, 
           std::chrono::milliseconds duration, int max_retries, 
           TaskWork work)
    : id_(id)
    , name_(std::move(name))
    , priority_(priority)
    , duration_(duration)
    , max_retries_(max_retries)
    , work_(std::move(work))
    , creation_time_(std::chrono::system_clock::now())
    , execute_after_(creation_time_)
{
}

TaskStatus Task::getStatus() {
    LockGuard lock(mutex_);
    return status_;
}

void Task::setStatus(TaskStatus status) {
    LockGuard lock(mutex_);
    status_ = status;
}

std::chrono::system_clock::time_point Task::getStartTime() {
    LockGuard lock(mutex_);
    return start_time_;
}

std::chrono::system_clock::time_point Task::getCompletionTime() {
    LockGuard lock(mutex_);
    return completion_time_;
}

std::chrono::system_clock::time_point Task::getExecuteAfter() const {
    LockGuard lock(mutex_);
    return execute_after_;
}

void Task::setExecuteAfter(std::chrono::system_clock::time_point execute_after) {
    LockGuard lock(mutex_);
    execute_after_ = execute_after;
}

int Task::getRetryCount() {
    LockGuard lock(mutex_);
    return retry_count_;
}

void Task::incrementRetryCount() {
    LockGuard lock(mutex_);
    retry_count_++;
}

void Task::cancel() {
    cancelled_ = true;
    LockGuard lock(mutex_);
    // Only set CANCELLED immediately if it's currently PENDING.
    // If it's RUNNING, the execution loop will cooperatively set it.
    if (status_ == TaskStatus::PENDING) {
        status_ = TaskStatus::CANCELLED;
    }
}

bool Task::isCancelled() {
    return cancelled_;
}

bool Task::execute() {
    {
        LockGuard lock(mutex_);
        if (status_ == TaskStatus::CANCELLED || cancelled_) {
            status_ = TaskStatus::CANCELLED;
            completion_time_ = std::chrono::system_clock::now();
            return false;
        }
        status_ = TaskStatus::RUNNING;
        start_time_ = std::chrono::system_clock::now();
    }

    bool success = false;

    if (work_) {
        // Run the custom callback task
        try {
            // Check cancellation before calling custom work
            if (isCancelled()) {
                success = false;
            } else {
                success = work_();
            }
        } catch (...) {
            success = false;
        }
    } else {
        // Run simulated task: sleep in increments to allow cooperative cancellation check
        auto start = std::chrono::steady_clock::now();
        success = true;
        
        while (std::chrono::steady_clock::now() - start < duration_) {
            if (isCancelled()) {
                success = false;
                break;
            }
            this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }

    {
        LockGuard lock(mutex_);
        completion_time_ = std::chrono::system_clock::now();
        
        if (cancelled_) {
            status_ = TaskStatus::CANCELLED;
            success = false;
        } else if (success) {
            status_ = TaskStatus::COMPLETED;
        } else {
            status_ = TaskStatus::FAILED;
        }
    }

    return success;
}
