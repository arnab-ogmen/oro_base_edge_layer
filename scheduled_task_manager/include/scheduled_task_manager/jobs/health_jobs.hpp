#ifndef SCHEDULED_TASK_MANAGER_JOBS_HEALTH_JOBS_HPP
#define SCHEDULED_TASK_MANAGER_JOBS_HEALTH_JOBS_HPP

#include "scheduled_task_manager/job.hpp"
#include "storage_handoff/storage_handoff.hpp"
#include <nlohmann/json.hpp>

namespace oro::stm::jobs {

/// health_signal_evaluator — Every 30 minutes.
/// Evaluates food/water/activity patterns and detects anomalies.
JobResult health_signal_evaluator(const nlohmann::json &config,
                                  storage_handoff::StorageWriter &writer);

/// baseline_recalculation — Daily.
/// Recalculates 7-day behavioral baselines for anomaly detection.
JobResult baseline_recalculation(const nlohmann::json &config,
                                 storage_handoff::StorageWriter &writer);

/// Prepare all SQL statements used by health jobs.
void prepare_health_job_statements(storage_handoff::StorageWriter &writer);

} // namespace oro::stm::jobs

#endif // SCHEDULED_TASK_MANAGER_JOBS_HEALTH_JOBS_HPP
