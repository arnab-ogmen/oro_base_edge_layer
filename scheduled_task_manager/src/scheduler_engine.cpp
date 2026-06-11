#include "scheduled_task_manager/scheduler_engine.hpp"
#include <iostream>

namespace oro::stm {

SchedulerEngine::SchedulerEngine(const SchedulerConfig& config, const JobRegistry& registry, JobExecutor& executor)
    : config_(config), registry_(registry), executor_(executor) {}

SchedulerEngine::~SchedulerEngine() {
    stop();
}

void SchedulerEngine::start() {
    if (running_.exchange(true)) {
        return;
    }

    std::cout << "[SchedulerEngine] Starting engine...\n";

    // Initialize last run times to (now - interval) so they run immediately on startup
    auto now = std::chrono::steady_clock::now();
    for (const auto& job : registry_.jobs()) {
        last_run_times_[job.name] = now - std::chrono::seconds(job.interval_seconds);
    }

    // Start worker threads
    int num_workers = config_.worker_threads();
    std::cout << "[SchedulerEngine] Spawning " << num_workers << " worker threads\n";
    for (int i = 0; i < num_workers; ++i) {
        workers_.emplace_back(&SchedulerEngine::worker_loop, this, i);
    }

    // Start tick thread
    tick_thread_ = std::thread(&SchedulerEngine::tick_loop, this);
}

void SchedulerEngine::stop() {
    if (!running_.exchange(false)) {
        return;
    }

    std::cout << "[SchedulerEngine] Stopping engine...\n";

    // Wake up workers to finish
    queue_cv_.notify_all();

    if (tick_thread_.joinable()) {
        tick_thread_.join();
    }

    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    workers_.clear();

    std::cout << "[SchedulerEngine] Engine stopped.\n";
}

void SchedulerEngine::tick_loop() {
    int interval_ms = config_.tick_interval_ms();
    while (running_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
        if (!running_) break;
        scan_and_enqueue_due_jobs();
    }
}

void SchedulerEngine::scan_and_enqueue_due_jobs() {
    auto now = std::chrono::steady_clock::now();

    for (const auto& job : registry_.jobs()) {
        if (!job.enabled) {
            continue;
        }

        auto it = last_run_times_.find(job.name);
        if (it == last_run_times_.end()) {
            last_run_times_[job.name] = now;
            continue;
        }

        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - it->second).count();
        if (elapsed >= job.interval_seconds) {
            // Update last run time to now (sliding window scheduling)
            last_run_times_[job.name] = now;

            std::cout << "[SchedulerEngine] Enqueueing due job '" << job.name << "' (Priority: " 
                      << priority_to_string(job.priority) << ")\n";

            {
                std::lock_guard<std::mutex> lock(queue_mutex_);
                queue_.push(QueueItem{job});
            }
            queue_cv_.notify_one();
        }
    }
}

void SchedulerEngine::worker_loop(int worker_id) {
    std::cout << "[SchedulerEngine] Worker " << worker_id << " ready.\n";

    while (running_) {
        QueueItem item;
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_cv_.wait(lock, [this]() {
                return !queue_.empty() || !running_;
            });

            if (!running_) {
                break;
            }

            item = queue_.top();
            queue_.pop();
        }

        // Execute job via JobExecutor (handles database lock gate internally)
        executor_.execute(item.job, config_.raw_config());
    }

    std::cout << "[SchedulerEngine] Worker " << worker_id << " exiting.\n";
}

} // namespace oro::stm
