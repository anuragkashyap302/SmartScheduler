#pragma once

#ifndef WINVER
#define WINVER 0x0600 // Target Windows Vista or later to enable CONDITION_VARIABLE APIs
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif

#include <windows.h>
#include <chrono>
#include <functional>
#include <memory>
#include <tuple>
#include <utility>

// Fallback implementation of std::apply for GCC compatibility
namespace concurrency_detail {
    template <typename F, typename Tuple, size_t... I>
    void apply_impl(F&& f, Tuple&& t, std::index_sequence<I...>) {
        std::forward<F>(f)(std::get<I>(std::forward<Tuple>(t))...);
    }

    template <typename F, typename Tuple>
    void apply(F&& f, Tuple&& t) {
        apply_impl(
            std::forward<F>(f), std::forward<Tuple>(t),
            std::make_index_sequence<std::tuple_size<typename std::decay<Tuple>::type>::value>{}
        );
    }
}

// Custom Mutex wrapper using Win32 CRITICAL_SECTION
class Mutex {
public:
    Mutex() {
        InitializeCriticalSection(&cs_);
    }
    ~Mutex() {
        DeleteCriticalSection(&cs_);
    }
    void lock() {
        EnterCriticalSection(&cs_);
    }
    void unlock() {
        LeaveCriticalSection(&cs_);
    }

    Mutex(const Mutex&) = delete;
    Mutex& operator=(const Mutex&) = delete;

private:
    friend class ConditionVariable;
    CRITICAL_SECTION cs_;
};

// Custom LockGuard (equivalent to std::lock_guard)
class LockGuard {
public:
    explicit LockGuard(Mutex& m) : m_(m) {
        m_.lock();
    }
    ~LockGuard() {
        m_.unlock();
    }

    LockGuard(const LockGuard&) = delete;
    LockGuard& operator=(const LockGuard&) = delete;

private:
    Mutex& m_;
};

// Custom UniqueLock (equivalent to std::unique_lock)
class UniqueLock {
public:
    explicit UniqueLock(Mutex& m) : m_(m), locked_(true) {
        m_.lock();
    }
    ~UniqueLock() {
        if (locked_) {
            m_.unlock();
        }
    }
    void lock() {
        if (!locked_) {
            m_.lock();
            locked_ = true;
        }
    }
    void unlock() {
        if (locked_) {
            m_.unlock();
            locked_ = false;
        }
    }
    Mutex& mutex() { return m_; }

    UniqueLock(const UniqueLock&) = delete;
    UniqueLock& operator=(const UniqueLock&) = delete;

private:
    friend class ConditionVariable;
    Mutex& m_;
    bool locked_;
};

// Custom ConditionVariable using Win32 CONDITION_VARIABLE
class ConditionVariable {
public:
    ConditionVariable() {
        InitializeConditionVariable(&cv_);
    }
    ~ConditionVariable() = default;

    void notify_one() {
        WakeConditionVariable(&cv_);
    }

    void notify_all() {
        WakeAllConditionVariable(&cv_);
    }

    void wait(UniqueLock& lk) {
        SleepConditionVariableCS(&cv_, &lk.m_.cs_, INFINITE);
    }

    template<typename Clock, typename Duration>
    bool wait_until(UniqueLock& lk, const std::chrono::time_point<Clock, Duration>& abs_time) {
        auto now = Clock::now();
        if (abs_time <= now) {
            return true;
        }
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(abs_time - now).count();
        if (ms <= 0) ms = 1;
        return SleepConditionVariableCS(&cv_, &lk.m_.cs_, static_cast<DWORD>(ms)) != 0;
    }

    ConditionVariable(const ConditionVariable&) = delete;
    ConditionVariable& operator=(const ConditionVariable&) = delete;

private:
    CONDITION_VARIABLE cv_;
};

// Custom Thread using Win32 CreateThread
class Thread {
public:
    Thread() : handle_(NULL), id_(0) {}

    template<typename F, typename... Args>
    explicit Thread(F&& f, Args&&... args) {
        // Pack function and arguments into a single lambda
        auto task = [func = std::forward<F>(f), 
                     tup = std::make_tuple(std::forward<Args>(args)...)]() mutable {
            concurrency_detail::apply(std::move(func), std::move(tup));
        };

        using TaskType = decltype(task);
        auto pTask = std::make_unique<TaskType>(std::move(task));

        handle_ = CreateThread(
            NULL,
            0,
            &Thread::threadProc<TaskType>,
            pTask.get(),
            0,
            &id_
        );

        if (handle_) {
            pTask.release(); // Task payload is now owned by the OS thread callback
        }
    }

    ~Thread() {
        // We do not close the handle automatically if we want standard std::thread semantics (must join)
    }

    // Move Semantics
    Thread(Thread&& other) noexcept : handle_(other.handle_), id_(other.id_) {
        other.handle_ = NULL;
        other.id_ = 0;
    }

    Thread& operator=(Thread&& other) noexcept {
        if (this != &other) {
            if (handle_) CloseHandle(handle_);
            handle_ = other.handle_;
            id_ = other.id_;
            other.handle_ = NULL;
            other.id_ = 0;
        }
        return *this;
    }

    Thread(const Thread&) = delete;
    Thread& operator=(const Thread&) = delete;

    bool joinable() const {
        return handle_ != NULL;
    }

    void join() {
        if (handle_) {
            WaitForSingleObject(handle_, INFINITE);
            CloseHandle(handle_);
            handle_ = NULL;
            id_ = 0;
        }
    }

private:
    template<typename TaskType>
    static DWORD WINAPI threadProc(LPVOID lpParam) {
        std::unique_ptr<TaskType> pTask(static_cast<TaskType*>(lpParam));
        (*pTask)();
        return 0;
    }

    HANDLE handle_;
    DWORD id_;
};

namespace this_thread {
    inline void sleep_for(const std::chrono::milliseconds& ms) {
        Sleep(static_cast<DWORD>(ms.count()));
    }
}
