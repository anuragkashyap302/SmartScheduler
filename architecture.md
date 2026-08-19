# System Architecture & Component Interaction Guide

This document provides a deep dive into the design patterns, sequence flows, data structures, and state transitions that form the core of the **SmartScheduler** engine.
<!-- full diagram with proper explanation of implentation -->
---

## 1. Class Structure and Domain Boundaries

The diagram below details the structural ownership and interface boundaries between components. The `Scheduler` acts as a facade, coordinating the storage (`registry`), immediate queueing (`TaskQueue`), future queueing (`delayed_queue`), and thread pools.

```mermaid
classDiagram
    class Scheduler {
        -next_task_id_ : atomic~TaskId~
        -stats_ : Statistics
        -taskQueue_ : TaskQueue
        -threadPool_ : ThreadPool
        -registry_ : unordered_map~TaskId, shared_ptr~Task~~
        -delayed_queue_ : priority_queue~DelayedTask~
        +start() void
        +shutdown() void
        +submitTask(name, priority, duration, max_retries, work) shared_ptr~Task~
        +submitTaskDelayed(name, priority, duration, delay, max_retries, work) shared_ptr~Task~
        +cancelTask(id) bool
        +getStats() StatsSnapshot
        +runTask(task, worker_id) void
    }

    class Task {
        -id_ : TaskId
        -name_ : string
        -priority_ : TaskPriority
        -duration_ : milliseconds
        -status_ : TaskStatus
        -cancelled_ : atomic~bool~
        -mutex_ : Mutex
        +execute() bool
        +cancel() void
        +isCancelled() bool
    }

    class TaskQueue {
        -queue_ : priority_queue~QueuedTask~
        -mutex_ : Mutex
        -cv_ : ConditionVariable
        +push(task) void
        +pop() shared_ptr~Task~
        +shutdown() void
    }

    class ThreadPool {
        -num_threads_ : size_t
        -workers_ : vector~Thread~
        -scheduler_ : Scheduler&
        +start() void
        +shutdown() void
        -worker_loop(worker_id) void
    }

    class Statistics {
        +total_submitted : atomic~uint32_t~
        +pending_tasks : atomic~uint32_t~
        +running_tasks : atomic~uint32_t~
        +completed_tasks : atomic~uint32_t~
        +failed_tasks : atomic~uint32_t~
        +cancelled_tasks : atomic~uint32_t~
        +total_execution_time_ms : atomic~uint64_t~
        +active_workers : atomic~uint32_t~
    }

    Scheduler "1" *-- "1" TaskQueue : owns
    Scheduler "1" *-- "1" ThreadPool : owns
    Scheduler "1" *-- "1" Statistics : owns
    Scheduler "1" *-- "*" Task : registers
    TaskQueue "1" o-- "*" Task : holds pointers
    ThreadPool "1" o-- "*" Thread : manages
```

---

## 2. Task Lifecycle State Machine

Tasks transit through several states during their existence. The diagram below maps how external calls (`submit`, `cancel`) and internal outcomes (`success`, `failure`, `retry`) drive these state transitions.

```mermaid
stateDiagram-v2
    [*] --> PENDING : submitTask()
    [*] --> DELAYED : submitTaskDelayed()
    
    DELAYED --> PENDING : Delay Expires
    DELAYED --> CANCELLED : cancelTask() [Immediate]
    
    PENDING --> CANCELLED : cancelTask() [Immediate]
    PENDING --> RUNNING : Worker Thread Pops Task
    
    RUNNING --> COMPLETED : Task Work Returns True / Success
    RUNNING --> CANCELLED : cancelTask() [Cooperative Interruption]
    
    RUNNING --> REQUEUED : Task Work Returns False / Fails
    REQUEUED --> PENDING : Max Retries Not Reached
    REQUEUED --> FAILED : Max Retries Reached
    
    COMPLETED --> [*]
    FAILED --> [*]
    CANCELLED --> [*]
```

---

## 3. Sequence Diagram: Task Lifecycle Flow

This diagram traces the exact temporal sequence and thread transitions when a user submits a task, when the delayed manager wakes up, and when a worker executes it.

```mermaid
sequenceDiagram
    autonumber
    actor User as User Thread (CLI)
    participant S as Scheduler
    participant DQ as Delayed Queue
    participant DT as Delayed Manager Thread
    participant AQ as Active TaskQueue
    participant W as Worker Thread Pool
    participant T as Task Object

    User->>S: submitTaskDelayed(name, priority, duration, delay)
    note over S: Generate TaskId<br/>Instantiate Task (PENDING)
    S->>DQ: Push (Task, execute_after)
    S->>User: Return Task Reference

    Note over DT: Sleeping on CV (wait_until next task time)
    Note over DT: Time expires or new early task notifies CV
    DT->>DQ: Pop Due Task
    DT->>AQ: Push (Task)
    Note over AQ: Queue is sorted by Priority, then FIFO sequence

    Note over W: Worker sleeping on AQ.pop() CV
    AQ->>W: Pop Ready Task
    Note over W: Worker updates Scheduler Stats to RUNNING

    W->>T: execute()
    loop Cooperative Sleep
        T->>T: Check if cancelled_ atomic is true
        Note over T: Sleep for 50ms increments
    end
    T->>W: Returns Success / Failure
    
    note over W: Update Stats (COMPLETED/FAILED/CANCELLED)<br/>Release task reference
```

---

## 4. Data Storage and Memory Management

1. **The Registry (`registry_`)**:
   * **Type**: `std::unordered_map<TaskId, std::shared_ptr<Task>>`
   * **Purpose**: Prevents garbage collection of task instances during execution, provides instantaneous lookup for CLI queries (`status`, `cancel`), and maintains memory tracking.
   * **Locking Strategy**: Protected by a dedicated, local `Mutex registry_mutex_`.
2. **Active Priority Queue (`queue_`)**:
   * **Type**: `std::priority_queue<QueuedTask>`
   * **Purpose**: Buffers tasks that are ready for immediate execution, sorted by Priority with FIFO fallback.
   * **Locking Strategy**: Protected internally by `TaskQueue`'s `Mutex`.
3. **Delayed Priority Queue (`delayed_queue_`)**:
   * **Type**: `std::priority_queue<DelayedTask>` (Min-heap sorted by execution time)
   * **Purpose**: Holds future tasks.
   * **Locking Strategy**: Guarded by `delayed_mutex_`. Uses `delayed_cv_` to sleep and wake up the delayed worker thread.
