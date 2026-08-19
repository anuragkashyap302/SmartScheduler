#pragma once

#include <vector>
#include <atomic>
#include <memory>
#include "Concurrency.h"
#include <memory>

// Forward declaration to break circular dependency
class Scheduler;

class ThreadPool {
public:
    ThreadPool(size_t num_threads, Scheduler& scheduler);
    ~ThreadPool();

    // Disable copy/assignment
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    // Starts the worker threads
    void start();

    // Shuts down the thread pool, waking up workers and joining them
    void shutdown();

    // Returns the number of threads in the pool
    size_t getThreadCount() const { return num_threads_; }

private:
    void worker_loop(size_t worker_id);

    const size_t num_threads_;
    Scheduler& scheduler_;
    std::vector<Thread> workers_;
    std::atomic<bool> shutdown_{false};
    mutable Mutex mutex_;
    bool started_{false};
};
