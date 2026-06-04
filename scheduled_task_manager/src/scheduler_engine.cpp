#include "scheduled_task_manager/scheduler_engine.hpp"
#include <iostream>

namespace oro::stm {

SchedulerEngine::SchedulerEngine(SchedulerConfig &config,
                                 JobRegistry &registry, JobExecutor &executor)
    : config_(config), registry_(registry), executor_(executor) {}

SchedulerEngine::~SchedulerEngine() { stop(); }

void SchedulerEngine::start() {
  if (running_.load())
    return;

  running_ = true;

  // Initialize runtime state for all jobs
  auto now = std::chrono::steady_clock::now();
  {
    std::lock_guard<std::mutex> lk(state_mutex_);
    for (const auto &job : registry_.jobs()) {
      JobRuntimeState state;
      state.next_run_at = now; // Run all jobs on first tick
      state.is_running = false;
      state.consecutive_failures = 0;
      job_states_[job.name] = state;
    }
  }

  // Recover any missed jobs from before shutdown
  recover_missed_jobs();

  // Start worker threads (1-2 as configured)
  int num_workers = config_.worker_threads();
  if (num_workers < 1) num_workers = 1;
  if (num_workers > 2) num_workers = 2;
  std::cout << "[SchedulerEngine] Starting " << num_workers
            << " worker thread(s).\n";
  for (int i = 0; i < num_workers; ++i) {
    worker_threads_.emplace_back(&SchedulerEngine::worker_loop, this);
  }

  // Start the tick loop
  tick_thread_ = std::thread(&SchedulerEngine::tick_loop, this);
  std::cout << "[SchedulerEngine] Engine started with "
            << registry_.size() << " registered jobs.\n";
}

void SchedulerEngine::stop() {
  if (!running_.load())
    return;

  std::cout << "[SchedulerEngine] Shutting down...\n";
  running_ = false;

  // Wake up workers so they can exit
  queue_cv_.notify_all();

  if (tick_thread_.joinable()) {
    tick_thread_.join();
  }

  for (auto &t : worker_threads_) {
    if (t.joinable()) {
      t.join();
    }
  }
  worker_threads_.clear();

  std::cout << "[SchedulerEngine] Shutdown complete.\n";
}

void SchedulerEngine::tick_loop() {
  std::cout << "[SchedulerEngine] Tick loop started (interval="
            << config_.tick_interval_ms() << "ms).\n";

  while (running_.load()) {
    scan_and_enqueue_due_jobs();

    std::this_thread::sleep_for(
        std::chrono::milliseconds(config_.tick_interval_ms()));
  }

  std::cout << "[SchedulerEngine] Tick loop stopped.\n";
}

void SchedulerEngine::scan_and_enqueue_due_jobs() {
  auto now = std::chrono::steady_clock::now();

  for (const auto &job : registry_.jobs()) {
    if (!job.enabled)
      continue;

    std::lock_guard<std::mutex> lk(state_mutex_);
    auto &state = job_states_[job.name];

    // Skip if already running (non-blocking: never re-enter a running job)
    if (state.is_running)
      continue;

    // Check if job is due
    if (now >= state.next_run_at) {
      // Enqueue into the priority work queue
      {
        std::lock_guard<std::mutex> qlk(queue_mutex_);
        work_queue_.push(WorkItem{&job, job.priority});
      }
      queue_cv_.notify_one();

      // Mark as running and schedule next run
      state.is_running = true;
      state.next_run_at =
          now + std::chrono::seconds(job.interval_seconds);
    }
  }
}

void SchedulerEngine::recover_missed_jobs() {
  // On startup, all jobs have next_run_at = now, so they will run on
  // the first tick. This effectively handles missed jobs.
  //
  // TODO: Implement missed job recovery logic if needed.
  std::cout << "[SchedulerEngine] Missed job recovery: all jobs will run on "
               "first tick.\n";
}

void SchedulerEngine::worker_loop() {
  std::cout << "[SchedulerEngine] Worker thread started (tid="
            << std::this_thread::get_id() << ").\n";

  while (running_.load()) {
    const JobDefinition *job = nullptr;

    // Wait for work
    {
      std::unique_lock<std::mutex> lk(queue_mutex_);
      queue_cv_.wait_for(lk, std::chrono::milliseconds(500),
                         [this] { return !work_queue_.empty() || !running_; });

      if (!running_)
        break;
      if (work_queue_.empty())
        continue;

      auto item = work_queue_.top();
      work_queue_.pop();
      job = item.job;
    }

    if (!job)
      continue;

    // Execute the job
    auto result = executor_.execute(*job, config_.raw());

    // Update runtime state
    {
      std::lock_guard<std::mutex> lk(state_mutex_);
      auto &state = job_states_[job->name];
      state.last_run_at = std::chrono::steady_clock::now();
      state.is_running = false;
      if (result.success) {
        state.consecutive_failures = 0;
      } else {
        state.consecutive_failures++;
      }
    }
  }

  std::cout << "[SchedulerEngine] Worker thread stopped (tid="
            << std::this_thread::get_id() << ").\n";
}

} // namespace oro::stm
