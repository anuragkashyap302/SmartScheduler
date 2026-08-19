#include "ThreadPool.h"
#include "Scheduler.h"

ThreadPool::ThreadPool(size_t num_threads, Scheduler& scheduler)
    : num_threads_(num_threads)
    , scheduler_(scheduler)
{
}

ThreadPool::~ThreadPool(){
    shutdown();
}

void ThreadPool::start(){
    LockGuard lock(mutex_);
    if (started_) {
        return;
    }
    started_ = true;
    for (size_t i = 0; i < num_threads_; ++i) {
        workers_.emplace_back([this, worker_id = i + 1]() { worker_loop(worker_id); });
    }
}

void ThreadPool::shutdown(){
    {
        LockGuard lock(mutex_);
        if (shutdown_) {
            return;
        }
        shutdown_ = true;
    }

    // Join all workers. Since scheduler's queue is shut down, popTask() will return nullptr
    // and worker loops will terminate naturally.
    for (Thread& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    workers_.clear();
}

void ThreadPool::worker_loop(size_t worker_id){
    while (!shutdown_) {
        // popTask blocks until a task is available or the queue is shut down
        std::shared_ptr<Task> task = scheduler_.popTask();
        
        if (shutdown_ || !task) {
            break;
        }

        // Delegate execution to the Scheduler, which manages stats, retries, logging, etc.
        scheduler_.runTask(task, worker_id);
    }
}
