#ifndef SCHEDULED_TASK_MANAGER_JOBS_SUMMARY_JOBS_HPP
#define SCHEDULED_TASK_MANAGER_JOBS_SUMMARY_JOBS_HPP

#include "scheduled_task_manager/job.hpp"
#include "storage_handoff/storage_handoff.hpp"
#include <nlohmann/json.hpp>

namespace oro::stm::jobs {

JobResult daily_pet_summary_generator(const nlohmann::json& config,
                                      storage_handoff::StorageWriter& writer);

JobResult weekly_pet_summary_generator(const nlohmann::json& config,
                                       storage_handoff::StorageWriter& writer);

void prepare_summary_job_statements(storage_handoff::StorageWriter& writer);

} // namespace oro::stm::jobs

#endif // SCHEDULED_TASK_MANAGER_JOBS_SUMMARY_JOBS_HPP
