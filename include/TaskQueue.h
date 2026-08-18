#pragma once

#include "Task.h"
#include <queue>
#include "Concurrency.h"
#include <memory>

struct QueuedTask {
    std::shared_ptr<Task> task;
    uint64_t sequence;

    // Strict weak ordering for std::priority_queue.
    // In std::priority_queue, the element with the highest priority according to this operator is at the top.
    // We want HIGH priority tasks first. For equal priority, we want FIFO (earliest sequence number first).
    bool operator<(const QueuedTask& other) const {
        if (task->getPriority() != other.task->getPriority()) {
            // Sort by priority (higher priority value at the top)
            return task->getPriority() < other.task->getPriority();
        }
        // For equal priority, lower sequence number (first in) comes first (at the top)
        // Thus, we consider a higher sequence number to be "less than" a lower sequence number.
        return sequence > other.sequence;
    }
};

class TaskQueue {
public:
    TaskQueue() = default;
    
    // Disable copy/assignment
    TaskQueue(const TaskQueue&) = delete;
    TaskQueue& operator=(const TaskQueue&) = delete;

    // Pushes a task to the queue and notifies a waiting worker
    void push(std::shared_ptr<Task> task);

    // Blocking pop. Waits until a task is available or the queue is shut down.
    // Returns nullptr if shut down.
    std::shared_ptr<Task> pop();

    // Returns if the queue is empty
    bool empty() const;

    // Returns current active size of the queue
    size_t size() const;

    // Triggers shutdown and wakes up any blocked threads
    void shutdown();

private:
    mutable Mutex mutex_;
    std::priority_queue<QueuedTask> queue_;
    ConditionVariable cv_;
    bool shutdown_{false};
    uint64_t next_sequence_{0};
};
