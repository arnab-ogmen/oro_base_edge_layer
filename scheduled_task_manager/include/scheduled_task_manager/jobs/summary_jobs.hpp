#ifndef SCHEDULED_TASK_MANAGER_JOBS_SUMMARY_JOBS_HPP
#define SCHEDULED_TASK_MANAGER_JOBS_SUMMARY_JOBS_HPP

#include "scheduled_task_manager/job.hpp"
#include "storage_handoff/storage_handoff.hpp"
#include <nlohmann/json.hpp>

namespace oro::stm::jobs {

/// daily_pet_summary_generator — Daily, user's local time.
/// Aggregates signals into a daily summary written to oro_base_summary.
JobResult daily_pet_summary_generator(const nlohmann::json &config,
                                      storage_handoff::StorageWriter &writer);

/// weekly_pet_summary_generator — Weekly, user's local time.
/// Aggregates daily summaries into a weekly report.
JobResult weekly_pet_summary_generator(const nlohmann::json &config,
                                       storage_handoff::StorageWriter &writer);

/// Prepare all SQL statements used by summary jobs.
void prepare_summary_job_statements(storage_handoff::StorageWriter &writer);

} // namespace oro::stm::jobs

#endif // SCHEDULED_TASK_MANAGER_JOBS_SUMMARY_JOBS_HPP
