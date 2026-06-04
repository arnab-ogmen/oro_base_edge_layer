#ifndef SCHEDULED_TASK_MANAGER_JOBS_CARE_JOBS_HPP
#define SCHEDULED_TASK_MANAGER_JOBS_CARE_JOBS_HPP

#include "scheduled_task_manager/job.hpp"
#include "storage_handoff/storage_handoff.hpp"
#include <nlohmann/json.hpp>

namespace oro::stm::jobs {

/// care_reminder_dispatch — Every 1 minute.
/// Checks if any care/feeding schedule is due and emits notification events.
JobResult care_reminder_dispatch(const nlohmann::json &config,
                                 storage_handoff::StorageWriter &writer);

/// overdue_task_checker — Every 15 minutes.
/// Scans care tasks past due, marks overdue, and emits alert events.
JobResult overdue_task_checker(const nlohmann::json &config,
                               storage_handoff::StorageWriter &writer);

/// Prepare all SQL statements used by care jobs. Must be called once at init.
void prepare_care_job_statements(storage_handoff::StorageWriter &writer);

} // namespace oro::stm::jobs

#endif // SCHEDULED_TASK_MANAGER_JOBS_CARE_JOBS_HPP
