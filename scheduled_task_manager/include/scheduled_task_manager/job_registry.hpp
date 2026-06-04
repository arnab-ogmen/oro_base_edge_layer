#ifndef SCHEDULED_TASK_MANAGER_JOB_REGISTRY_HPP
#define SCHEDULED_TASK_MANAGER_JOB_REGISTRY_HPP

#include "scheduled_task_manager/job.hpp"
#include "scheduled_task_manager/job_executor.hpp"
#include "scheduled_task_manager/scheduler_config.hpp"
#include <string>
#include <unordered_map>
#include <vector>

namespace oro::stm {

/**
 * @brief Central registry of all scheduled jobs.
 *
 * Currently config-driven: all 11 jobs from the specification are registered
 * at startup with their handlers, frequencies, and priorities.
 *
 * TODO: Make this database-driven for runtime job management.
 *       When migrating, add methods to:
 *       - load_from_database(StorageWriter &writer)
 *       - sync_config_to_database()  // seed DB from config
 *       - refresh()  // hot-reload job definitions from DB
 *       The registry should query the scheduled_jobs table for enabled jobs
 *       and merge with config overrides.
 */
class JobRegistry {
public:
  /**
   * @brief Initialize the registry with all known jobs.
   * @param config The scheduler configuration for frequency/enable overrides.
   * @param executor Reference to the job executor for command pushing.
   */
  void initialize(const SchedulerConfig &config, JobExecutor &executor);

  /**
   * @brief Get all registered job definitions.
   */
  const std::vector<JobDefinition> &jobs() const { return jobs_; }

  /**
   * @brief Look up a job by name.
   * @return Pointer to the job definition, or nullptr if not found.
   */
  const JobDefinition *find(const std::string &name) const;

  /**
   * @brief Get the number of registered jobs.
   */
  size_t size() const { return jobs_.size(); }

private:
  std::vector<JobDefinition> jobs_;
  std::unordered_map<std::string, size_t> name_index_;

  void register_job(JobDefinition job);
};

} // namespace oro::stm

#endif // SCHEDULED_TASK_MANAGER_JOB_REGISTRY_HPP
