#include "Scheduler.h"
#include <iostream>
#include <cassert>
#include "Concurrency.h"
#include <atomic>
#include <chrono>

#define ASSERT_TRUE(x) do { if (!(x)) { std::cerr << "FAIL: " << #x << " at " << __FILE__ << ":" << __LINE__ << std::endl; std::exit(1); } } while(0)
#define ASSERT_FALSE(x) ASSERT_TRUE(!(x))
#define ASSERT_EQ(x, y) ASSERT_TRUE((x) == (y))

void testTaskCreation() {
    std::cout << "Running testTaskCreation..." << std::endl;
    Task task(1, "Test Task", TaskPriority::HIGH, std::chrono::milliseconds(500), 2);
    ASSERT_EQ(task.getId(), 1);
    ASSERT_EQ(task.getName(), "Test Task");
    ASSERT_EQ(task.getPriority(), TaskPriority::HIGH);
    ASSERT_EQ(task.getStatus(), TaskStatus::PENDING);
    ASSERT_EQ(task.getMaxRetries(), 2);
    ASSERT_EQ(task.getRetryCount(), 0);
    std::cout << "testTaskCreation PASSED" << std::endl;
}

void testPriorityOrdering() {
    std::cout << "Running testPriorityOrdering..." << std::endl;
    TaskQueue queue;
    auto taskLow = std::make_shared<Task>(1, "Low", TaskPriority::LOW, std::chrono::milliseconds(100));
    auto taskMed = std::make_shared<Task>(2, "Med", TaskPriority::MEDIUM, std::chrono::milliseconds(100));
    auto taskHigh = std::make_shared<Task>(3, "High", TaskPriority::HIGH, std::chrono::milliseconds(100));
    auto taskHigh2 = std::make_shared<Task>(4, "High2", TaskPriority::HIGH, std::chrono::milliseconds(100));

    // Push out of order
    queue.push(taskLow);
    queue.push(taskHigh);
    queue.push(taskMed);
    queue.push(taskHigh2); // Same priority as taskHigh

    // Pop should return HIGH (first pushed), then HIGH (second pushed), then MEDIUM, then LOW (FIFO fallback on equal priority)
    auto pop1 = queue.pop();
    ASSERT_EQ(pop1->getId(), 3); // High first

    auto pop2 = queue.pop();
    ASSERT_EQ(pop2->getId(), 4); // High2 second (FIFO fallback)

    auto pop3 = queue.pop();
    ASSERT_EQ(pop3->getId(), 2); // Med

    auto pop4 = queue.pop();
    ASSERT_EQ(pop4->getId(), 1); // Low
    std::cout << "testPriorityOrdering PASSED" << std::endl;
}

void testTaskSubmission() {
    std::cout << "Running testTaskSubmission..." << std::endl;
    Scheduler scheduler(2);
    scheduler.start();

    auto task = scheduler.submitTask("Task 1", TaskPriority::HIGH, std::chrono::milliseconds(50));
    ASSERT_TRUE(task != nullptr);
    ASSERT_EQ(task->getName(), "Task 1");

    // Wait for task to finish
    this_thread::sleep_for(std::chrono::milliseconds(150));
    ASSERT_EQ(task->getStatus(), TaskStatus::COMPLETED);
    
    scheduler.shutdown();
    std::cout << "testTaskSubmission PASSED" << std::endl;
}

void testConcurrentExecution() {
    std::cout << "Running testConcurrentExecution..." << std::endl;
    Scheduler scheduler(3);
    scheduler.start();

    auto start = std::chrono::steady_clock::now();

    // Submit 3 tasks that take 300ms each.
    // If they run concurrently with 3 workers, total time should be close to 300ms, not 900ms.
    auto t1 = scheduler.submitTask("T1", TaskPriority::LOW, std::chrono::milliseconds(300));
    auto t2 = scheduler.submitTask("T2", TaskPriority::LOW, std::chrono::milliseconds(300));
    auto t3 = scheduler.submitTask("T3", TaskPriority::LOW, std::chrono::milliseconds(300));

    this_thread::sleep_for(std::chrono::milliseconds(450));

    ASSERT_EQ(t1->getStatus(), TaskStatus::COMPLETED);
    ASSERT_EQ(t2->getStatus(), TaskStatus::COMPLETED);
    ASSERT_EQ(t3->getStatus(), TaskStatus::COMPLETED);

    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
    ASSERT_TRUE(duration < 800); // Definitely less than sequential 900ms

    scheduler.shutdown();
    std::cout << "testConcurrentExecution PASSED" << std::endl;
}

void testCancellation() {
    std::cout << "Running testCancellation..." << std::endl;
    Scheduler scheduler(1);
    scheduler.start();

    // 1. Cancel pending task
    // Submit a long task to occupy the single worker thread
    auto busyTask = scheduler.submitTask("Busy", TaskPriority::HIGH, std::chrono::milliseconds(500));
    
    // Submit a pending task
    auto pendingTask = scheduler.submitTask("Pending", TaskPriority::HIGH, std::chrono::milliseconds(200));
    
    // Cancel the pending task
    bool cancelled = scheduler.cancelTask(pendingTask->getId());
    ASSERT_TRUE(cancelled);
    ASSERT_EQ(pendingTask->getStatus(), TaskStatus::CANCELLED);

    // Wait for the busy task to complete so the worker becomes free
    this_thread::sleep_for(std::chrono::milliseconds(500));

    // 2. Cancel running task (cooperative cancellation)
    // Submit a task with a long execution duration
    auto runningTask = scheduler.submitTask("Running", TaskPriority::HIGH, std::chrono::milliseconds(1000));
    
    // Wait a brief moment for it to start running
    this_thread::sleep_for(std::chrono::milliseconds(100));
    ASSERT_EQ(runningTask->getStatus(), TaskStatus::RUNNING);

    // Cancel running task
    cancelled = scheduler.cancelTask(runningTask->getId());
    ASSERT_TRUE(cancelled);
    // Since cooperative, wait a tiny bit for it to stop and set status to CANCELLED
    this_thread::sleep_for(std::chrono::milliseconds(150));
    ASSERT_EQ(runningTask->getStatus(), TaskStatus::CANCELLED);

    scheduler.shutdown();
    std::cout << "testCancellation PASSED" << std::endl;
}

void testRetryMechanism() {
    std::cout << "Running testRetryMechanism..." << std::endl;
    Scheduler scheduler(2);
    scheduler.start();

    // Submit a task that fails once, then succeeds.
    // Retries = 2.
    std::atomic<int> counter{0};
    auto task = scheduler.submitTask("Retry Task", TaskPriority::HIGH, std::chrono::milliseconds(10), 2, [&counter]() {
        counter++;
        if (counter == 1) return false; // Fail first time
        return true;                    // Succeed second time
    });

    this_thread::sleep_for(std::chrono::milliseconds(150));

    ASSERT_EQ(task->getStatus(), TaskStatus::COMPLETED);
    ASSERT_EQ(task->getRetryCount(), 1); // 1 retry attempt
    ASSERT_EQ(counter.load(), 2);        // Ran exactly twice

    scheduler.shutdown();
    std::cout << "testRetryMechanism PASSED" << std::endl;
}

void testStatistics() {
    std::cout << "Running testStatistics..." << std::endl;
    Scheduler scheduler(2);
    scheduler.start();

    scheduler.submitTask("S1", TaskPriority::HIGH, std::chrono::milliseconds(50));
    scheduler.submitTask("S2", TaskPriority::LOW, std::chrono::milliseconds(50));

    this_thread::sleep_for(std::chrono::milliseconds(150));

    auto stats = scheduler.getStats();
    ASSERT_EQ(stats.total_submitted, 2);
    ASSERT_EQ(stats.completed_tasks, 2);
    ASSERT_EQ(stats.failed_tasks, 0);
    ASSERT_EQ(stats.running_tasks, 0);

    scheduler.shutdown();
    std::cout << "testStatistics PASSED" << std::endl;
}

void testSchedulerShutdown() {
    std::cout << "Running testSchedulerShutdown..." << std::endl;
    Scheduler scheduler(2);
    scheduler.start();

    // Submit some long running tasks
    auto t1 = scheduler.submitTask("ST1", TaskPriority::LOW, std::chrono::milliseconds(1000));
    auto t2 = scheduler.submitTask("ST2", TaskPriority::LOW, std::chrono::milliseconds(1000));

    this_thread::sleep_for(std::chrono::milliseconds(50));
    
    // Shutdown immediately
    scheduler.shutdown();

    // Threads should have terminated and scheduler shutdown should complete without waiting 1 second
    // (since shutdown joins threads, but the main thing is that we exited quickly)
    std::cout << "testSchedulerShutdown PASSED" << std::endl;
}

void testEmptyQueueBehavior() {
    std::cout << "Running testEmptyQueueBehavior..." << std::endl;
    TaskQueue queue;
    
    // Create a thread that pops from empty queue. It should block.
    bool popped = false;
    Thread t([&queue, &popped]() {
        auto task = queue.pop();
        if (task == nullptr) {
            popped = true; // Returns nullptr on shutdown
        }
    });

    this_thread::sleep_for(std::chrono::milliseconds(100));
    ASSERT_FALSE(popped); // Thread is still blocked waiting

    // Shutdown the queue
    queue.shutdown();
    
    t.join();
    ASSERT_TRUE(popped); // Thread unblocked and popped nullptr
    std::cout << "testEmptyQueueBehavior PASSED" << std::endl;
}

int main() {
    std::cout << "=== RUNNING SMART SCHEDULER TESTS ===\n" << std::endl;

    testTaskCreation();
    testPriorityOrdering();
    testTaskSubmission();
    testConcurrentExecution();
    testCancellation();
    testRetryMechanism();
    testStatistics();
    testSchedulerShutdown();
    testEmptyQueueBehavior();

    std::cout << "\nALL TESTS PASSED SUCCESSFULLY!" << std::endl;
    return 0;
}
