#ifndef SCHEDULED_TASK_MANAGER_JOB_HPP
#define SCHEDULED_TASK_MANAGER_JOB_HPP

#include <chrono>
#include <cstdint>
#include <functional>
#include <nlohmann/json.hpp>
#include <string>

// Forward declaration to avoid heavy header inclusion
namespace storage_handoff {
class StorageWriter;
}

namespace oro::stm {

// ──────────────────────────────────────────────────────────────
// Job priority levels (lower value = higher priority)
// ──────────────────────────────────────────────────────────────
enum class JobPriority : uint8_t {
  CRITICAL = 0, // Health alerts, device safety
  HIGH = 1,     // Care reminders, notifications
  MEDIUM = 2,   // Retries, sync
  LOW = 3       // Summaries, cleanup, baseline
};

// ──────────────────────────────────────────────────────────────
// Job category — maps to the PDF §3 Job Categories
// ──────────────────────────────────────────────────────────────
enum class JobCategory : uint8_t {
  REMINDER,
  HEALTH,
  DEVICE,
  SUMMARY,
  RETRY,
  CLEANUP
};

// ──────────────────────────────────────────────────────────────
// Execution result returned by every job handler
// ──────────────────────────────────────────────────────────────
struct JobResult {
  bool success{false};
  std::string error;
  int items_processed{0};
  nlohmann::json metadata;
};

// ──────────────────────────────────────────────────────────────
// Job handler signature — every job implements this
// Jobs receive config (for settings) AND a StorageWriter (for DB queries)
// ──────────────────────────────────────────────────────────────
using JobHandler = std::function<JobResult(const nlohmann::json &config,
                                           storage_handoff::StorageWriter &writer)>;

// ──────────────────────────────────────────────────────────────
// Job definition (config-driven)
// ──────────────────────────────────────────────────────────────
struct JobDefinition {
  std::string name;            // Unique job identifier (e.g. "care_reminder_dispatch")
  std::string display_name;    // Human-readable name
  JobCategory category;
  JobPriority priority;
  int interval_seconds;        // How often the job should run
  bool enabled{true};          // Can be toggled via config
  int max_retries{4};          // Max retry attempts before dead-letter
  int timeout_seconds{30};     // Max execution time before timeout
  JobHandler handler;          // The actual job function
};

// ──────────────────────────────────────────────────────────────
// Runtime state tracked per job (in-memory, not persisted)
// ──────────────────────────────────────────────────────────────
struct JobRuntimeState {
  std::chrono::steady_clock::time_point last_run_at{};
  std::chrono::steady_clock::time_point next_run_at{};
  int consecutive_failures{0};
  bool is_running{false};
};

// ──────────────────────────────────────────────────────────────
// Helpers
// ──────────────────────────────────────────────────────────────
inline const char *priority_to_string(JobPriority p) {
  switch (p) {
  case JobPriority::CRITICAL:
    return "CRITICAL";
  case JobPriority::HIGH:
    return "HIGH";
  case JobPriority::MEDIUM:
    return "MEDIUM";
  case JobPriority::LOW:
    return "LOW";
  }
  return "UNKNOWN";
}

inline const char *category_to_string(JobCategory c) {
  switch (c) {
  case JobCategory::REMINDER:
    return "REMINDER";
  case JobCategory::HEALTH:
    return "HEALTH";
  case JobCategory::DEVICE:
    return "DEVICE";
  case JobCategory::SUMMARY:
    return "SUMMARY";
  case JobCategory::RETRY:
    return "RETRY";
  case JobCategory::CLEANUP:
    return "CLEANUP";
  }
  return "UNKNOWN";
}

} // namespace oro::stm

#endif // SCHEDULED_TASK_MANAGER_JOB_HPP
