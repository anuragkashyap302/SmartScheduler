#include "TaskQueue.h"

void TaskQueue::push(std::shared_ptr<Task> task) {
    LockGuard lock(mutex_);
    if (shutdown_) {
        return;
    }
    queue_.push({std::move(task), next_sequence_++});
    cv_.notify_one();
}

std::shared_ptr<Task> TaskQueue::pop() {
    UniqueLock lock(mutex_);
    while (queue_.empty() && !shutdown_) {
        cv_.wait(lock);
    }
    if (shutdown_) {
        return nullptr;
    }
    QueuedTask queued = queue_.top();
    queue_.pop();
    return queued.task;
}

bool TaskQueue::empty() const {
    LockGuard lock(mutex_);
    return queue_.empty();
}

size_t TaskQueue::size() const {
    LockGuard lock(mutex_);
    return queue_.size();
}

void TaskQueue::shutdown() {
    LockGuard lock(mutex_);
    shutdown_ = true;
    // Clear the queue to release shared pointers immediately
    while (!queue_.empty()) {
        queue_.pop();
    }
    cv_.notify_all();
}
