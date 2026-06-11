#ifndef SCHEDULED_TASK_MANAGER_JOBS_DEVICE_JOBS_HPP
#define SCHEDULED_TASK_MANAGER_JOBS_DEVICE_JOBS_HPP

#include "scheduled_task_manager/job.hpp"
#include "storage_handoff/storage_handoff.hpp"
#include <nlohmann/json.hpp>

namespace oro::stm::jobs {

/**
 * @brief device_health_check: Checks device battery levels and status reports.
 */
JobResult device_health_check(const nlohmann::json& config,
                              storage_handoff::StorageWriter& writer);

/**
 * @brief sensor_data_freshness_check: Verifies sensor data streams are updated within thresholds.
 */
JobResult sensor_data_freshness_check(const nlohmann::json& config,
                                      storage_handoff::StorageWriter& writer);

/**
 * @brief Prepares SQL statements needed for device jobs.
 */
void prepare_device_job_statements(storage_handoff::StorageWriter& writer);

} // namespace oro::stm::jobs

#endif // SCHEDULED_TASK_MANAGER_JOBS_DEVICE_JOBS_HPP
