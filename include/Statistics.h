#pragma once

#include <atomic>
#include <cstdint>

struct Statistics {
    std::atomic<uint32_t> total_submitted{0};
    std::atomic<uint32_t> pending_tasks{0};
    std::atomic<uint32_t> running_tasks{0};
    std::atomic<uint32_t> completed_tasks{0};
    std::atomic<uint32_t> failed_tasks{0};
    std::atomic<uint32_t> cancelled_tasks{0};
    std::atomic<uint64_t> total_execution_time_ms{0}; // Tracked in ms to compute average
    std::atomic<uint32_t> active_workers{0};          // Workers currently running tasks
};
