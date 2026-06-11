#include "scheduled_task_manager/job_registry.hpp"
#include "scheduled_task_manager/jobs/care_jobs.hpp"
#include "scheduled_task_manager/jobs/device_jobs.hpp"
#include "scheduled_task_manager/jobs/health_jobs.hpp"
#include "scheduled_task_manager/jobs/cleanup_jobs.hpp"
#include "scheduled_task_manager/jobs/retry_jobs.hpp"
#include "scheduled_task_manager/jobs/summary_jobs.hpp"
#include <iostream>

namespace oro::stm {

void JobRegistry::register_job(JobDefinition job) {
    name_index_[job.name] = jobs_.size();
    jobs_.push_back(std::move(job));
}

const JobDefinition* JobRegistry::find(const std::string& name) const {
    auto it = name_index_.find(name);
    if (it != name_index_.end()) {
        return &jobs_[it->second];
    }
    return nullptr;
}

void JobRegistry::initialize(const SchedulerConfig& config) {
    std::cout << "[JobRegistry] Initializing scheduled jobs...\n";

    auto make_job = [](const char* name, const char* display, JobCategory cat,
                       JobPriority pri, int interval, bool enabled, int retries,
                       int timeout, JobHandler handler) -> JobDefinition {
        JobDefinition j;
        j.name = name;
        j.display_name = display;
        j.category = cat;
        j.priority = pri;
        j.interval_seconds = interval;
        j.enabled = enabled;
        j.max_retries = retries;
        j.timeout_seconds = timeout;
        j.handler = std::move(handler);
        return j;
    };

    // --- REMINDER JOBS ---
    register_job(make_job("care_reminder_dispatch", "Care Reminder Dispatch",
        JobCategory::REMINDER, JobPriority::HIGH,
        config.job_interval_seconds("care_reminder_dispatch", 60),
        config.is_job_enabled("care_reminder_dispatch"), 2, 10,
        jobs::care_reminder_dispatch));

    register_job(make_job("overdue_task_checker", "Overdue Task Checker",
        JobCategory::REMINDER, JobPriority::HIGH,
        config.job_interval_seconds("overdue_task_checker", 900),
        config.is_job_enabled("overdue_task_checker"), 2, 15,
        jobs::overdue_task_checker));


    // --- DEVICE JOBS ---
    register_job(make_job("device_health_check", "Device Health Check",
        JobCategory::DEVICE, JobPriority::CRITICAL,
        config.job_interval_seconds("device_health_check", 300),
        config.is_job_enabled("device_health_check"), 3, 20,
        jobs::device_health_check));

    register_job(make_job("sensor_data_freshness_check", "Sensor Data Freshness",
        JobCategory::DEVICE, JobPriority::CRITICAL,
        config.job_interval_seconds("sensor_data_freshness_check", 600),
        config.is_job_enabled("sensor_data_freshness_check"), 3, 15,
        jobs::sensor_data_freshness_check));

    // --- HEALTH JOBS ---
    /*
    register_job(make_job("health_signal_evaluator", "Health Signal Evaluator",
        JobCategory::HEALTH, JobPriority::CRITICAL,
        config.job_interval_seconds("health_signal_evaluator", 1800),
        config.is_job_enabled("health_signal_evaluator"), 2, 30,
        jobs::health_signal_evaluator));

    register_job(make_job("baseline_recalculation", "Baseline Recalculation (STUB)",
        JobCategory::HEALTH, JobPriority::LOW,
        config.job_interval_seconds("baseline_recalculation", 86400),
        config.is_job_enabled("baseline_recalculation"), 2, 60,
        jobs::baseline_recalculation));
    */

    // --- SUMMARY JOBS (STUBS) ---
    /*
    register_job(make_job("daily_pet_summary_generator", "Daily Pet Summary (STUB)",
        JobCategory::SUMMARY, JobPriority::LOW,
        config.job_interval_seconds("daily_pet_summary_generator", 86400),
        config.is_job_enabled("daily_pet_summary_generator"), 3, 60,
        jobs::daily_pet_summary_generator));

    register_job(make_job("weekly_pet_summary_generator", "Weekly Pet Summary (STUB)",
        JobCategory::SUMMARY, JobPriority::LOW,
        config.job_interval_seconds("weekly_pet_summary_generator", 604800),
        config.is_job_enabled("weekly_pet_summary_generator"), 3, 120,
        jobs::weekly_pet_summary_generator));
    */

    // --- RETRY JOBS (STUBS) ---
    /*
    register_job(make_job("failed_sync_retry", "Failed Sync Retry (STUB)",
        JobCategory::RETRY, JobPriority::MEDIUM,
        config.job_interval_seconds("failed_sync_retry", 300),
        config.is_job_enabled("failed_sync_retry"), 0, 30,
        jobs::failed_sync_retry));

    register_job(make_job("notification_retry", "Notification Retry (STUB)",
        JobCategory::RETRY, JobPriority::MEDIUM,
        config.job_interval_seconds("notification_retry", 300),
        config.is_job_enabled("notification_retry"), 0, 30,
        jobs::notification_retry));
    */

    // --- CLEANUP JOBS (STUB) ---
    /*
    register_job(make_job("data_cleanup", "Data Cleanup (STUB)",
        JobCategory::CLEANUP, JobPriority::LOW,
        config.job_interval_seconds("data_cleanup", 86400),
        config.is_job_enabled("data_cleanup"), 1, 120,
        jobs::data_cleanup));
    */

    std::cout << "[JobRegistry] Registered " << jobs_.size() << " jobs.\n";
    for (const auto& job : jobs_) {
        std::cout << "  [" << (job.enabled ? "ON " : "OFF") << "] " << job.name
                  << " - interval: " << job.interval_seconds << "s, priority: "
                  << priority_to_string(job.priority) << "\n";
    }
}

} // namespace oro::stm
