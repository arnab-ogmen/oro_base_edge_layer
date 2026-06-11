#ifndef SCHEDULED_TASK_MANAGER_JOBS_HEALTH_JOBS_HPP
#define SCHEDULED_TASK_MANAGER_JOBS_HEALTH_JOBS_HPP

#include "scheduled_task_manager/job.hpp"
#include "storage_handoff/storage_handoff.hpp"
#include <nlohmann/json.hpp>

namespace oro::stm::jobs {

/**
 * @brief health_signal_evaluator: Evaluates behavioral and vital health signals against 7-day baselines.
 */
JobResult health_signal_evaluator(const nlohmann::json& config,
                                  storage_handoff::StorageWriter& writer);

/**
 * @brief baseline_recalculation: Refreshes behavioral baselines (stub in this phase).
 */
JobResult baseline_recalculation(const nlohmann::json& config,
                                 storage_handoff::StorageWriter& writer);

/**
 * @brief Prepares SQL statements needed for health jobs.
 */
void prepare_health_job_statements(storage_handoff::StorageWriter& writer);

} // namespace oro::stm::jobs

#endif // SCHEDULED_TASK_MANAGER_JOBS_HEALTH_JOBS_HPP
