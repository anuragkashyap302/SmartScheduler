#pragma once

#include <string>
#include "Concurrency.h"
#include <iostream>
#include <chrono>
#include <iomanip>
#include <ctime>

class Logger {
public:
    // Thread-safe log message with high-precision timestamp
    static void log(const std::string& message) {
        LockGuard lock(getMutex());
        
        auto now = std::chrono::system_clock::now();
        auto in_time_t = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()
        ) % 1000;
        
        std::tm* tm_ptr = std::localtime(&in_time_t);
        if (tm_ptr) {
            std::cout << "[" << std::put_time(tm_ptr, "%H:%M:%S") << "." 
                      << std::setfill('0') << std::setw(3) << ms.count() << "] "
                      << message << std::endl;
        } else {
            std::cout << "[time_error] " << message << std::endl;
        }
    }

private:
    // Shared mutex to coordinate logs from multiple worker threads
    static Mutex& getMutex() {
        static Mutex mutex;
        return mutex;
    }
};
