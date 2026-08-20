#include "Scheduler.h"
#include "Logger.h"
#include <iostream>
#include <sstream>
#include <vector>
#include <string>
#include <algorithm>
#include "Concurrency.h"
#include <iomanip>

// Helper to tokenize command lines, handling quotes correctly
std::vector<std::string> tokenize(const std::string& input) {
    std::vector<std::string> tokens;
    std::string token;
    bool in_quotes = false;
    for (size_t i = 0; i < input.length(); ++i) {
        char c = input[i];
        if (c == '"') {
            in_quotes = !in_quotes;
        } else if (c == ' ' && !in_quotes) {
            if (!token.empty()) {
                tokens.push_back(token);
                token.clear();
            }
        } else {
            token.push_back(c);
        }
    }
    if (!token.empty()) {
        tokens.push_back(token);
    }
    return tokens;
}

// Convert string to TaskPriority case-insensitively
bool parsePriority(std::string str, TaskPriority& priority) {
    std::transform(str.begin(), str.end(), str.begin(), ::toupper);
    if (str == "HIGH") {
        priority = TaskPriority::HIGH;
        return true;
    } else if (str == "MEDIUM") {
        priority = TaskPriority::MEDIUM;
        return true;
    } else if (str == "LOW") {
        priority = TaskPriority::LOW;
        return true;
    }
    return false;
}

void printHelp() {
    std::cout << "\nAvailable Commands:\n"
              << "  submit <name> <priority> <duration> [--delay <delay_sec>] [--retries <max_retries>]\n"
              << "        Submit a task. Priority: HIGH|MEDIUM|LOW. Duration/Delay in seconds.\n"
              << "        Example: submit \"Database Backup\" HIGH 5 --delay 2\n"
              << "  cancel <task_id>\n"
              << "        Cancel a pending task or request cancellation of a running task.\n"
              << "  status <task_id>\n"
              << "        Show detailed status of a task.\n"
              << "  list\n"
              << "        List all submitted tasks and their statuses.\n"
              << "  stats\n"
              << "        Show scheduler statistics.\n"
              << "  demo\n"
              << "        Run the concurrent execution and priority scheduling demo.\n"
              << "  help\n"
              << "        Display this help text.\n"
              << "  shutdown\n"
              << "        Gracefully shut down the scheduler.\n"
              << "  exit\n"
              << "        Shut down the scheduler and exit the application.\n" << std::endl;
}

void runDemo(Scheduler& scheduler) {
    Logger::log("[DEMO] Starting demonstration scenario...");
    Logger::log("[DEMO] Step 1: Submitting 4 tasks to occupy all 4 worker threads (concurrency check)");

    // Occupy workers
    scheduler.submitTask("Occupy Worker-1", TaskPriority::LOW, std::chrono::seconds(4));
    scheduler.submitTask("Occupy Worker-2", TaskPriority::LOW, std::chrono::seconds(4));
    scheduler.submitTask("Occupy Worker-3", TaskPriority::LOW, std::chrono::seconds(4));
    scheduler.submitTask("Occupy Worker-4", TaskPriority::LOW, std::chrono::seconds(4));

    // Wait a brief moment to allow workers to start execution
    this_thread::sleep_for(std::chrono::milliseconds(200));

    Logger::log("[DEMO] Step 2: Workers are busy. Now submitting queued tasks with different priorities to show ordering");
    
    // These tasks will be queued since all workers are busy
    scheduler.submitTask("LOW Priority Task #1", TaskPriority::LOW, std::chrono::seconds(2));
    scheduler.submitTask("HIGH Priority Task #2", TaskPriority::HIGH, std::chrono::seconds(2));
    scheduler.submitTask("MEDIUM Priority Task #3", TaskPriority::MEDIUM, std::chrono::seconds(2));
    scheduler.submitTask("HIGH Priority Task #4", TaskPriority::HIGH, std::chrono::seconds(2));
    scheduler.submitTask("LOW Priority Task #5", TaskPriority::LOW, std::chrono::seconds(2));

    Logger::log("[DEMO] Step 3: Pushing a delayed task to demonstrate future execution");
    // Scheduled to execute after 5 seconds
    scheduler.submitTaskDelayed("Delayed High Priority Task #6", TaskPriority::HIGH, std::chrono::seconds(2), std::chrono::seconds(5));

    Logger::log("[DEMO] Step 4: Pushing a failing task with retry count of 2 to show retry mechanism");
    // Custom task work that fails the first two times, then succeeds
    auto retryCounter = std::make_shared<std::atomic<int>>(0);
    scheduler.submitTask("Self-Healing Task #7", TaskPriority::HIGH, std::chrono::seconds(1), 2, [retryCounter]() {
        int attempt = retryCounter->fetch_add(1);
        if (attempt < 2) {
            Logger::log("[WORK] Task #7: Simulating failure (Attempt " + std::to_string(attempt + 1) + ")");
            return false; // Triggers failure -> retry
        }
        Logger::log("[WORK] Task #7: Success on attempt " + std::to_string(attempt + 1));
        return true;
    });

    Logger::log("[DEMO] Wait for workers to complete tasks. Observe HIGH priority tasks popped before LOW priority tasks.");
    Logger::log("[DEMO] You can run commands (e.g. 'list', 'stats') concurrently while the demo runs!");
}

int main() {
    std::cout << "==================================================\n"
              << "         MULTITHREADED TASK SCHEDULER CLI         \n"
              << "==================================================\n"
              << "System Workers: 4\n"
              << "Type 'help' for commands, or 'demo' to run the demo.\n"
              << "==================================================\n" << std::endl;

    Scheduler scheduler(4);
    scheduler.start();

    std::string line;
    while (true) {
        std::cout << "> ";
        if (!std::getline(std::cin, line)) {
            break;
        }

        std::vector<std::string> tokens = tokenize(line);
        if (tokens.empty()) {
            continue;
        }

        std::string command = tokens[0];
        std::transform(command.begin(), command.end(), command.begin(), ::tolower);

        if (command == "exit" || command == "quit") {
            scheduler.shutdown();
            break;
        } 
        else if (command == "shutdown") {
            scheduler.shutdown();
        } 
        else if (command == "help") {
            printHelp();
        } 
        else if (command == "stats") {
            scheduler.printStats();
        } 
        else if (command == "demo") {
            runDemo(scheduler);
        } 
        else if (command == "cancel") {
            if (tokens.size() < 2) {
                std::cout << "Usage: cancel <task_id>" << std::endl;
                continue;
            }
            try {
                uint32_t id = std::stoul(tokens[1]);
                scheduler.cancelTask(id);
            } catch (const std::exception& e) {
                std::cout << "Error: Invalid task ID (" << e.what() << ")" << std::endl;
            }
        } 
        else if (command == "status") {
            if (tokens.size() < 2) {
                std::cout << "Usage: status <task_id>" << std::endl;
                continue;
            }
            try {
                uint32_t id = std::stoul(tokens[1]);
                auto task = scheduler.getTask(id);
                if (task) {
                    std::cout << "\nTask Details:\n"
                              << "  ID            : " << task->getId() << "\n"
                              << "  Name          : " << task->getName() << "\n"
                              << "  Priority      : " << priorityToString(task->getPriority()) << "\n"
                              << "  Status        : " << statusToString(task->getStatus()) << "\n"
                              << "  Duration      : " << task->getDuration().count() / 1000.0 << " sec\n"
                              << "  Retries       : " << task->getRetryCount() << " / " << task->getMaxRetries() << "\n";
                    
                    auto creation = std::chrono::system_clock::to_time_t(task->getCreationTime());
                    std::tm* tm_ptr = std::localtime(&creation);
                    if (tm_ptr) {
                        std::cout << "  Created At    : " << std::put_time(tm_ptr, "%H:%M:%S") << "\n";
                    }

                    if (task->getStatus() != TaskStatus::PENDING) {
                        auto start = std::chrono::system_clock::to_time_t(task->getStartTime());
                        tm_ptr = std::localtime(&start);
                        if (tm_ptr) {
                            std::cout << "  Started At    : " << std::put_time(tm_ptr, "%H:%M:%S") << "\n";
                        }
                    }
                    if (task->getStatus() == TaskStatus::COMPLETED || task->getStatus() == TaskStatus::FAILED || task->getStatus() == TaskStatus::CANCELLED) {
                        auto completion = std::chrono::system_clock::to_time_t(task->getCompletionTime());
                        tm_ptr = std::localtime(&completion);
                        if (tm_ptr) {
                            std::cout << "  Completed At  : " << std::put_time(tm_ptr, "%H:%M:%S") << "\n";
                        }
                    }
                    std::cout << std::endl;
                } else {
                    std::cout << "Task #" << id << " not found" << std::endl;
                }
            } catch (const std::exception& e) {
                std::cout << "Error: Invalid task ID (" << e.what() << ")" << std::endl;
            }
        } 
        else if (command == "list") {
            auto tasks = scheduler.getAllTasks();
            if (tasks.empty()) {
                std::cout << "No tasks submitted yet." << std::endl;
                continue;
            }
            // Sort tasks by ID for clean listing
            std::sort(tasks.begin(), tasks.end(), [](const auto& a, const auto& b) {
                return a->getId() < b->getId();
            });

            std::cout << "\n" << std::left 
                      << std::setw(5)  << "ID"
                      << std::setw(30) << "Name"
                      << std::setw(10) << "Priority"
                      << std::setw(12) << "Status"
                      << std::setw(10) << "Duration"
                      << std::setw(10) << "Retries"
                      << "\n" << std::string(77, '-') << "\n";

            for (const auto& task : tasks) {
                std::cout << std::left
                          << std::setw(5)  << task->getId()
                          << std::setw(30) << (task->getName().length() > 27 ? task->getName().substr(0, 24) + "..." : task->getName())
                          << std::setw(10) << priorityToString(task->getPriority())
                          << std::setw(12) << statusToString(task->getStatus())
                          << std::setw(10) << std::to_string(task->getDuration().count() / 1000.0) + "s"
                          << std::to_string(task->getRetryCount()) + "/" + std::to_string(task->getMaxRetries())
                          << "\n";
            }
            std::cout << std::endl;
        } 
        else if (command == "submit") {
            if (tokens.size() < 4) {
                std::cout << "Usage: submit <name> <priority> <duration> [--delay <delay_sec>] [--retries <max_retries>]" << std::endl;
                continue;
            }

            std::string name = tokens[1];
            TaskPriority priority;
            if (!parsePriority(tokens[2], priority)) {
                std::cout << "Error: Invalid priority '" << tokens[2] << "'. Must be HIGH, MEDIUM, or LOW." << std::endl;
                continue;
            }

            double duration_sec = 0.0;
            try {
                duration_sec = std::stod(tokens[3]);
                if (duration_sec < 0) throw std::out_of_range("Negative duration");
            } catch (const std::exception&) {
                std::cout << "Error: Invalid duration '" << tokens[3] << "'. Must be a positive number." << std::endl;
                continue;
            }

            // Parse optional flags
            double delay_sec = 0.0;
            int retries = 0;
            bool valid_flags = true;

            for (size_t i = 4; i < tokens.size(); i += 2) {
                if (i + 1 >= tokens.size()) {
                    std::cout << "Error: Missing value for flag '" << tokens[i] << "'" << std::endl;
                    valid_flags = false;
                    break;
                }

                if (tokens[i] == "--delay") {
                    try {
                        delay_sec = std::stod(tokens[i+1]);
                        if (delay_sec < 0) throw std::out_of_range("Negative delay");
                    } catch (...) {
                        std::cout << "Error: Invalid delay value '" << tokens[i+1] << "'" << std::endl;
                        valid_flags = false;
                        break;
                    }
                } else if (tokens[i] == "--retries") {
                    try {
                        retries = std::stoi(tokens[i+1]);
                        if (retries < 0) throw std::out_of_range("Negative retries");
                    } catch (...) {
                        std::cout << "Error: Invalid retries value '" << tokens[i+1] << "'" << std::endl;
                        valid_flags = false;
                        break;
                    }
                } else {
                    std::cout << "Error: Unknown flag '" << tokens[i] << "'" << std::endl;
                    valid_flags = false;
                    break;
                }
            }

            if (!valid_flags) {
                continue;
            }

            auto duration_ms = std::chrono::milliseconds(static_cast<long long>(duration_sec * 1000.0));
            auto delay_ms = std::chrono::milliseconds(static_cast<long long>(delay_sec * 1000.0));

            if (delay_sec > 0.0) {
                scheduler.submitTaskDelayed(name, priority, duration_ms, delay_ms, retries);
            } else {
                scheduler.submitTask(name, priority, duration_ms, retries);
            }
        } 
        else {
            std::cout << "Unknown command. Type 'help' to see list of available commands." << std::endl;
        }
    }

    return 0;
}
