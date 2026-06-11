#ifndef SCHEDULED_TASK_MANAGER_JOB_HPP
#define SCHEDULED_TASK_MANAGER_JOB_HPP

#include <string>
#include <functional>
#include <chrono>
#include <unordered_map>
#include <nlohmann/json.hpp>
#include "storage_handoff/storage_handoff.hpp"

namespace oro::stm {

enum class JobCategory {
    REMINDER,
    HEALTH,
    DEVICE,
    SUMMARY,
    RETRY,
    CLEANUP
};

enum class JobPriority {
    CRITICAL,
    HIGH,
    MEDIUM,
    LOW
};

inline std::string category_to_string(JobCategory cat) {
    switch (cat) {
        case JobCategory::REMINDER: return "reminder";
        case JobCategory::HEALTH:   return "health";
        case JobCategory::DEVICE:   return "device";
        case JobCategory::SUMMARY:  return "summary";
        case JobCategory::RETRY:    return "retry";
        case JobCategory::CLEANUP:  return "cleanup";
    }
    return "unknown";
}

inline std::string priority_to_string(JobPriority pri) {
    switch (pri) {
        case JobPriority::CRITICAL: return "CRITICAL";
        case JobPriority::HIGH:     return "HIGH";
        case JobPriority::MEDIUM:   return "MEDIUM";
        case JobPriority::LOW:      return "LOW";
    }
    return "UNKNOWN";
}

struct JobResult {
    bool success = false;
    int items_processed = 0;
    std::string error;
    nlohmann::json metadata = nlohmann::json::object();
};

// Handlers accept global config and database storage writer reference.
using JobHandler = std::function<JobResult(const nlohmann::json&, storage_handoff::StorageWriter&)>;

struct JobDefinition {
    std::string name;
    std::string display_name;
    JobCategory category;
    JobPriority priority;
    int interval_seconds = 60;
    bool enabled = true;
    int max_retries = 3;
    int timeout_seconds = 30;
    JobHandler handler;
};

} // namespace oro::stm

#endif // SCHEDULED_TASK_MANAGER_JOB_HPP
