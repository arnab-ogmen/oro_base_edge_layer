#ifndef SCHEDULED_TASK_MANAGER_SCHEDULER_ENGINE_HPP
#define SCHEDULED_TASK_MANAGER_SCHEDULER_ENGINE_HPP

#include "scheduled_task_manager/job.hpp"
#include "scheduled_task_manager/job_executor.hpp"
#include "scheduled_task_manager/job_registry.hpp"
#include "scheduled_task_manager/scheduler_config.hpp"
#include <string>

namespace oro::stm {

class SchedulerEngine {
public:
    SchedulerEngine(const SchedulerConfig& config, const JobRegistry& registry, JobExecutor& executor);
    ~SchedulerEngine();

    bool generate_cron_config(const std::string& cron_file_path, const std::string& binary_path);

private:
    const SchedulerConfig& config_;
    const JobRegistry& registry_;
    JobExecutor& executor_;
};

} // namespace oro::stm

#endif // SCHEDULED_TASK_MANAGER_SCHEDULER_ENGINE_HPP
