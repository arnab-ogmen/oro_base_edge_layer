#ifndef SCHEDULED_TASK_MANAGER_SCHEDULER_ENGINE_HPP
#define SCHEDULED_TASK_MANAGER_SCHEDULER_ENGINE_HPP

#include "scheduled_task_manager/job.hpp"
#include "scheduled_task_manager/job_executor.hpp"
#include "scheduled_task_manager/job_registry.hpp"
#include "scheduled_task_manager/scheduler_config.hpp"
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace oro::stm {

struct QueueItem {
    JobDefinition job;
    
    // Sort priority: CRITICAL > HIGH > MEDIUM > LOW
    // Lower enum value represents higher priority
    bool operator<(const QueueItem& other) const {
        return static_cast<int>(job.priority) > static_cast<int>(other.job.priority);
    }
};

class SchedulerEngine {
public:
    SchedulerEngine(const SchedulerConfig& config, const JobRegistry& registry, JobExecutor& executor);
    ~SchedulerEngine();

    void start();
    void stop();

private:
    void tick_loop();
    void worker_loop(int worker_id);
    void scan_and_enqueue_due_jobs();

    const SchedulerConfig& config_;
    const JobRegistry& registry_;
    JobExecutor& executor_;

    std::atomic<bool> running_{false};
    std::thread tick_thread_;
    std::vector<std::thread> workers_;

    // Priority work queue
    std::priority_queue<QueueItem> queue_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;

    // Track last execution time (steady_clock) for each job to handle intervals
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> last_run_times_;
};

} // namespace oro::stm

#endif // SCHEDULED_TASK_MANAGER_SCHEDULER_ENGINE_HPP
