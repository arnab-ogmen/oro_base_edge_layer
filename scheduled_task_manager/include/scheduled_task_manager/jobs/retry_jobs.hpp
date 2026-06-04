#ifndef SCHEDULED_TASK_MANAGER_JOBS_RETRY_JOBS_HPP
#define SCHEDULED_TASK_MANAGER_JOBS_RETRY_JOBS_HPP

#include "scheduled_task_manager/job.hpp"
#include "storage_handoff/storage_handoff.hpp"
#include <nlohmann/json.hpp>

namespace oro::stm::jobs {

/// failed_sync_retry — Every 5 minutes. Placeholder.
/// TODO: Integrate with existing sync_queue.db in storage_handoff.
JobResult failed_sync_retry(const nlohmann::json &config,
                            storage_handoff::StorageWriter &writer);

/// notification_retry — Every 5 minutes. Placeholder.
/// TODO: Implement after notification delivery pathway is decided.
JobResult notification_retry(const nlohmann::json &config,
                             storage_handoff::StorageWriter &writer);

void prepare_retry_job_statements(storage_handoff::StorageWriter &writer);

} // namespace oro::stm::jobs

#endif // SCHEDULED_TASK_MANAGER_JOBS_RETRY_JOBS_HPP
