#ifndef SCHEDULED_TASK_MANAGER_JOB_EXECUTOR_HPP
#define SCHEDULED_TASK_MANAGER_JOB_EXECUTOR_HPP

#include "scheduled_task_manager/job.hpp"
#include "scheduled_task_manager/lock_manager.hpp"
#include "storage_handoff/storage_handoff.hpp"
#include <zmq.hpp>
#include <string>
#include <nlohmann/json.hpp>

namespace oro::stm {

class JobExecutor {
public:
    JobExecutor(storage_handoff::StorageWriter& writer, LockManager& lock_manager, std::string owner_id);
    ~JobExecutor() = default;

    /**
     * @brief Prepare the SQL statements required for the executor.
     */
    void prepare_statements();

    /**
     * @brief Executes a job by managing lock acquisition, executing its handler,
     *        measuring performance, logging the results, and releasing the lock.
     */
    JobResult execute(const JobDefinition& job, const nlohmann::json& config);

    /**
     * @brief Helper to push ZMQ command events to the command executor.
     */
    bool push_command(uint16_t signal_id, const std::string& signal_type, const nlohmann::json& payload);

private:
    void log_execution(const std::string& job_name, const std::string& status,
                       const std::string& started_at, int duration_ms,
                       int items_processed, const std::string& error_msg,
                       const nlohmann::json& metadata);

    storage_handoff::StorageWriter& writer_;
    LockManager& lock_manager_;
    std::string owner_id_;
    zmq::context_t zmq_context_;
    zmq::socket_t cmd_push_socket_;
};

} // namespace oro::stm

#endif // SCHEDULED_TASK_MANAGER_JOB_EXECUTOR_HPP
