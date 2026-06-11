#ifndef SCHEDULED_TASK_MANAGER_JOBS_CARE_JOBS_HPP
#define SCHEDULED_TASK_MANAGER_JOBS_CARE_JOBS_HPP

#include "scheduled_task_manager/job.hpp"
#include "storage_handoff/storage_handoff.hpp"
#include <nlohmann/json.hpp>

namespace oro::stm::jobs {

/**
 * @brief care_reminder_dispatch: Scans feeding and care schedules due now,
 *        and inserts notifications.
 */
JobResult care_reminder_dispatch(const nlohmann::json& config,
                                 storage_handoff::StorageWriter& writer);

/**
 * @brief overdue_task_checker: Checks for incomplete/past-due schedules,
 *        marks them overdue, and flags notifications.
 */
JobResult overdue_task_checker(const nlohmann::json& config,
                               storage_handoff::StorageWriter& writer);

/**
 * @brief Prepares SQL statements needed for care jobs.
 */
void prepare_care_job_statements(storage_handoff::StorageWriter& writer);

} // namespace oro::stm::jobs

#endif // SCHEDULED_TASK_MANAGER_JOBS_CARE_JOBS_HPP
