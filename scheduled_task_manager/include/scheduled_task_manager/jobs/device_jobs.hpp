#ifndef SCHEDULED_TASK_MANAGER_JOBS_DEVICE_JOBS_HPP
#define SCHEDULED_TASK_MANAGER_JOBS_DEVICE_JOBS_HPP

#include "scheduled_task_manager/job.hpp"
#include "storage_handoff/storage_handoff.hpp"
#include <nlohmann/json.hpp>

namespace oro::stm::jobs {

/// device_health_check — Every 5 minutes.
/// Evaluates device connectivity, battery, supply levels.
JobResult device_health_check(const nlohmann::json &config,
                              storage_handoff::StorageWriter &writer);

/// sensor_data_freshness_check — Every 10 minutes.
/// Checks if monitoring nodes are producing data within expected windows.
JobResult sensor_data_freshness_check(const nlohmann::json &config,
                                      storage_handoff::StorageWriter &writer);

/// Prepare all SQL statements used by device jobs.
void prepare_device_job_statements(storage_handoff::StorageWriter &writer);

} // namespace oro::stm::jobs

#endif // SCHEDULED_TASK_MANAGER_JOBS_DEVICE_JOBS_HPP
