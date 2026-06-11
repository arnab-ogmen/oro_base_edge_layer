#ifndef SCHEDULED_TASK_MANAGER_JOBS_RETRY_JOBS_HPP
#define SCHEDULED_TASK_MANAGER_JOBS_RETRY_JOBS_HPP

#include "scheduled_task_manager/job.hpp"
#include "storage_handoff/storage_handoff.hpp"
#include <nlohmann/json.hpp>

namespace oro::stm::jobs {

JobResult failed_sync_retry(const nlohmann::json& config,
                            storage_handoff::StorageWriter& writer);

JobResult notification_retry(const nlohmann::json& config,
                             storage_handoff::StorageWriter& writer);

void prepare_retry_job_statements(storage_handoff::StorageWriter& writer);

} // namespace oro::stm::jobs

#endif // SCHEDULED_TASK_MANAGER_JOBS_RETRY_JOBS_HPP
