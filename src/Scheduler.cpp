#include "Scheduler.h"
#include "Logger.h"
#include <iostream>

Scheduler::Scheduler(size_t num_workers)
    : threadPool_(num_workers, *this)
{
}

Scheduler::~Scheduler() {
    shutdown();
}

void Scheduler::start() {
    LockGuard lock(registry_mutex_);
    if (started_) {
        return;
    }
    started_ = true;
    
    // Start worker thread pool
    threadPool_.start();

    // Start delayed task worker thread
    delayed_worker_thread_ = Thread([this]() { delayedTaskWorkerLoop(); });
    Logger::log("[SYSTEM] Scheduler started with " + std::to_string(threadPool_.getThreadCount()) + " worker threads");
}

void Scheduler::shutdown() {
    if (scheduler_shutdown_.exchange(true)) {
        return;
    }

    Logger::log("[SYSTEM] Shutting down scheduler gracefully...");

    // Stop delayed task manager
    delayed_shutdown_ = true;
    delayed_cv_.notify_all();
    if (delayed_worker_thread_.joinable()) {
        delayed_worker_thread_.join();
    }

    // Stop active task queue (wakes up thread pool workers)
    taskQueue_.shutdown();

    // Stop thread pool
    threadPool_.shutdown();

    Logger::log("[SYSTEM] Scheduler shutdown complete");
}

std::shared_ptr<Task> Scheduler::submitTask(const std::string& name, TaskPriority priority, 
                                            std::chrono::milliseconds duration, int max_retries,
                                            Task::TaskWork work) {
    if (scheduler_shutdown_) {
        return nullptr;
    }

    Task::TaskId id = next_task_id_++;
    auto task = std::make_shared<Task>(id, name, priority, duration, max_retries, std::move(work));

    {
        LockGuard lock(registry_mutex_);
        registry_[id] = task;
    }

    stats_.total_submitted++;
    stats_.pending_tasks++;

    taskQueue_.push(task);
    Logger::log("[SUBMIT] Task #" + std::to_string(id) + " | " + priorityToString(priority) + " | " + name);

    return task;
}

std::shared_ptr<Task> Scheduler::submitTaskDelayed(const std::string& name, TaskPriority priority, 
                                                   std::chrono::milliseconds duration, 
                                                   std::chrono::milliseconds delay, int max_retries,
                                                   Task::TaskWork work) {
    if (scheduler_shutdown_) {
        return nullptr;
    }

    Task::TaskId id = next_task_id_++;
    auto task = std::make_shared<Task>(id, name, priority, duration, max_retries, std::move(work));
    
    auto execute_after = std::chrono::system_clock::now() + delay;
    task->setExecuteAfter(execute_after);

    {
        LockGuard lock(registry_mutex_);
        registry_[id] = task;
    }

    stats_.total_submitted++;

    {
        LockGuard lock(delayed_mutex_);
        delayed_queue_.push({task, execute_after});
        delayed_cv_.notify_one();
    }

    Logger::log("[SUBMIT DELAYED] Task #" + std::to_string(id) + " | " + priorityToString(priority) + " | " + name + " | Delay: " + std::to_string(delay.count() / 1000.0) + " sec");

    return task;
}

bool Scheduler::cancelTask(Task::TaskId id) {
    std::shared_ptr<Task> task = getTask(id);
    if (!task) {
        Logger::log("[CANCEL] Task #" + std::to_string(id) + " not found");
        return false;
    }

    TaskStatus status = task->getStatus();
    if (status == TaskStatus::COMPLETED || status == TaskStatus::FAILED || status == TaskStatus::CANCELLED) {
        Logger::log("[CANCEL] Task #" + std::to_string(id) + " is already in finished state (" + statusToString(status) + ")");
        return false;
    }

    // Attempt cooperative cancellation
    task->cancel();

    // Check if the task was pending (either in the active queue or delayed queue)
    if (status == TaskStatus::PENDING) {
        auto now = std::chrono::system_clock::now();
        // If it was scheduled for the future and is still in the delayed queue
        if (task->getExecuteAfter() > now) {
            stats_.cancelled_tasks++;
            Logger::log("[CANCEL] Task #" + std::to_string(id) + " cancelled while pending in delayed queue");
        } else {
            stats_.pending_tasks--;
            stats_.cancelled_tasks++;
            Logger::log("[CANCEL] Task #" + std::to_string(id) + " cancelled while pending in active queue");
        }
    } else if (status == TaskStatus::RUNNING) {
        // running status remains RUNNING until worker thread detects cancellation and terminates
        Logger::log("[CANCEL] Cancellation requested for running Task #" + std::to_string(id));
    }

    return true;
}

std::shared_ptr<Task> Scheduler::getTask(Task::TaskId id) {
    LockGuard lock(registry_mutex_);
    auto it = registry_.find(id);
    if (it != registry_.end()) {
        return it->second;
    }
    return nullptr;
}

std::vector<std::shared_ptr<Task>> Scheduler::getAllTasks() {
    LockGuard lock(registry_mutex_);
    std::vector<std::shared_ptr<Task>> list;
    list.reserve(registry_.size());
    for (const auto& pair : registry_) {
        list.push_back(pair.second);
    }
    return list;
}

StatsSnapshot Scheduler::getStats() const {
    StatsSnapshot snapshot;
    snapshot.total_submitted = stats_.total_submitted.load();
    snapshot.pending_tasks = stats_.pending_tasks.load();
    snapshot.running_tasks = stats_.running_tasks.load();
    snapshot.completed_tasks = stats_.completed_tasks.load();
    snapshot.failed_tasks = stats_.failed_tasks.load();
    snapshot.cancelled_tasks = stats_.cancelled_tasks.load();
    snapshot.active_workers = stats_.active_workers.load();
    snapshot.current_queue_size = taskQueue_.size();

    uint64_t total_time = stats_.total_execution_time_ms.load();
    uint32_t completed = snapshot.completed_tasks;
    if (completed > 0) {
        snapshot.average_execution_time_sec = ((double)total_time / 1000.0) / completed;
    } else {
        snapshot.average_execution_time_sec = 0.0;
    }

    return snapshot;
}

void Scheduler::printStats() {
    StatsSnapshot snapshot = getStats();
    
    // Synchronized output block
    LockGuard lock(registry_mutex_);
    std::cout << "\n========================================\n"
              << "          SCHEDULER STATISTICS          \n"
              << "========================================\n"
              << "Total Submitted   : " << snapshot.total_submitted << "\n"
              << "Pending Tasks     : " << snapshot.pending_tasks << "\n"
              << "Running Tasks     : " << snapshot.running_tasks << "\n"
              << "Completed Tasks   : " << snapshot.completed_tasks << "\n"
              << "Failed Tasks      : " << snapshot.failed_tasks << "\n"
              << "Cancelled Tasks   : " << snapshot.cancelled_tasks << "\n"
              << "Active Workers    : " << snapshot.active_workers << " / " << threadPool_.getThreadCount() << "\n"
              << "Current Queue Size: " << snapshot.current_queue_size << "\n"
              << "Average Runtime   : " << std::fixed << std::setprecision(2) << snapshot.average_execution_time_sec << " sec\n"
              << "========================================\n" << std::endl;
}

std::shared_ptr<Task> Scheduler::popTask() {
    return taskQueue_.pop();
}

void Scheduler::runTask(std::shared_ptr<Task> task, size_t worker_id) {
    if (task->getStatus() == TaskStatus::CANCELLED) {
        // Discard task. Already counted in cancelTask.
        return;
    }

    stats_.pending_tasks--;
    stats_.running_tasks++;
    stats_.active_workers++;

    Logger::log("[Worker-" + std::to_string(worker_id) + "] Started Task #" + std::to_string(task->getId()) + " | " + task->getName());

    bool success = task->execute();

    stats_.running_tasks--;
    stats_.active_workers--;

    if (task->getStatus() == TaskStatus::COMPLETED) {
        stats_.completed_tasks++;
        
        auto start = task->getStartTime();
        auto end = task->getCompletionTime();
        auto execution_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
        stats_.total_execution_time_ms += execution_ms;

        Logger::log("[Worker-" + std::to_string(worker_id) + "] Completed Task #" + std::to_string(task->getId()) + " | Duration: " + std::to_string(execution_ms / 1000.0) + " sec");
    } 
    else if (task->getStatus() == TaskStatus::CANCELLED) {
        stats_.cancelled_tasks++;
        Logger::log("[Worker-" + std::to_string(worker_id) + "] Task #" + std::to_string(task->getId()) + " cancelled cooperatively");
    } 
    else if (task->getStatus() == TaskStatus::FAILED) {
        if (task->getRetryCount() < task->getMaxRetries()) {
            task->incrementRetryCount();
            requeueTask(task);
        } else {
            stats_.failed_tasks++;
            Logger::log("[Worker-" + std::to_string(worker_id) + "] Task #" + std::to_string(task->getId()) + " failed (Max retries reached)");
        }
    }
}

void Scheduler::requeueTask(std::shared_ptr<Task> task) {
    task->setStatus(TaskStatus::PENDING);
    stats_.pending_tasks++;
    taskQueue_.push(task);
    Logger::log("[RETRY] Task #" + std::to_string(task->getId()) + " | Requeuing (Attempt " + std::to_string(task->getRetryCount()) + ")");
}

void Scheduler::delayedTaskWorkerLoop() {
    UniqueLock lock(delayed_mutex_);
    while (!delayed_shutdown_) {
        if (delayed_queue_.empty()) {
            delayed_cv_.wait(lock);
        } else {
            auto now = std::chrono::system_clock::now();
            auto next_time = delayed_queue_.top().execute_after;

            if (now >= next_time) {
                // Delayed task is due!
                auto task = delayed_queue_.top().task;
                delayed_queue_.pop();

                // Unlock delayed task structures before modifying active queue to prevent deadlock
                lock.unlock();

                // Check if task was cancelled before pushing
                if (task->getStatus() != TaskStatus::CANCELLED) {
                    stats_.pending_tasks++;
                    taskQueue_.push(task);
                    Logger::log("[SCHEDULE] Delayed Task #" + std::to_string(task->getId()) + " is now ready | " + task->getName());
                } else {
                    Logger::log("[SCHEDULE] Delayed Task #" + std::to_string(task->getId()) + " skipped (cancelled while delayed)");
                }

                lock.lock();
            } else {
                // Wait until the target time or until we are notified (when a task with an earlier target time is added)
                delayed_cv_.wait_until(lock, next_time);
            }
        }
    }
}
