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
#include <unordered_map>
#include <vector>

namespace oro::stm {

/**
 * @brief Core scheduler engine — non-blocking, priority-queued job dispatcher.
 *
 * Architecture:
 *  - Main tick thread: scans for due jobs every tick_interval_ms, enqueues them
 *    into a priority work queue.
 *  - 1–2 worker threads: dequeue and execute jobs asynchronously.
 *  - Jobs are non-blocking: the tick thread never waits for a job to complete.
 *
 * On startup, checks for missed jobs (next_run_at in the past) and runs them
 * once if still relevant.
 */
class SchedulerEngine {
public:
  SchedulerEngine(SchedulerConfig &config, JobRegistry &registry,
                  JobExecutor &executor);
  ~SchedulerEngine();

  /**
   * @brief Start the scheduler engine (tick thread + worker pool).
   */
  void start();

  /**
   * @brief Stop the scheduler engine gracefully.
   * Waits for in-flight jobs to complete before returning.
   */
  void stop();

  /**
   * @brief Check if the engine is currently running.
   */
  bool is_running() const { return running_.load(); }

private:
  // ── Tick loop ───────────────────────────────────────────────
  void tick_loop();
  void scan_and_enqueue_due_jobs();
  void recover_missed_jobs();

  // ── Worker pool ─────────────────────────────────────────────
  void worker_loop();

  // ── Priority work queue item ────────────────────────────────
  struct WorkItem {
    const JobDefinition *job;
    JobPriority priority;

    // Min-heap: lower priority value = higher urgency
    bool operator>(const WorkItem &other) const {
      return static_cast<uint8_t>(priority) >
             static_cast<uint8_t>(other.priority);
    }
  };

  // ── Members ─────────────────────────────────────────────────
  SchedulerConfig &config_;
  JobRegistry &registry_;
  JobExecutor &executor_;

  std::atomic<bool> running_{false};
  std::thread tick_thread_;
  std::vector<std::thread> worker_threads_;

  // Priority work queue (min-heap by priority)
  std::priority_queue<WorkItem, std::vector<WorkItem>, std::greater<WorkItem>>
      work_queue_;
  std::mutex queue_mutex_;
  std::condition_variable queue_cv_;

  // Runtime state per job (keyed by job name)
  std::unordered_map<std::string, JobRuntimeState> job_states_;
  std::mutex state_mutex_;
};

} // namespace oro::stm

#endif // SCHEDULED_TASK_MANAGER_SCHEDULER_ENGINE_HPP
