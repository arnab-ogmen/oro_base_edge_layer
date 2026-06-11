#ifndef SCHEDULED_TASK_MANAGER_JOB_REGISTRY_HPP
#define SCHEDULED_TASK_MANAGER_JOB_REGISTRY_HPP

#include "scheduled_task_manager/job.hpp"
#include "scheduled_task_manager/scheduler_config.hpp"
#include <string>
#include <unordered_map>
#include <vector>

namespace oro::stm {

class JobRegistry {
public:
    JobRegistry() = default;
    ~JobRegistry() = default;

    /**
     * @brief Register all active and stub jobs based on config frequencies.
     */
    void initialize(const SchedulerConfig& config);

    const std::vector<JobDefinition>& jobs() const { return jobs_; }
    const JobDefinition* find(const std::string& name) const;
    size_t size() const { return jobs_.size(); }

private:
    void register_job(JobDefinition job);

    std::vector<JobDefinition> jobs_;
    std::unordered_map<std::string, size_t> name_index_;
};

} // namespace oro::stm

#endif // SCHEDULED_TASK_MANAGER_JOB_REGISTRY_HPP
