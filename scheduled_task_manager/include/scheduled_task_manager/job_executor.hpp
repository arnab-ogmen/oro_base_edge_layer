#ifndef SCHEDULED_TASK_MANAGER_JOB_EXECUTOR_HPP
#define SCHEDULED_TASK_MANAGER_JOB_EXECUTOR_HPP

#include "scheduled_task_manager/job.hpp"
#include "storage_handoff/storage_handoff.hpp"
#include <nlohmann/json.hpp>
#include <zmq.hpp>

namespace oro::stm {

/**
 * @brief Executes individual jobs and logs results to stdout/stderr.
 *
 * Responsibilities:
 *  - Invoke the job handler with its config context.
 *  - For hardware-action jobs, push synthesized Commands to command_executor
 *    via ZMQ IPC.
 */
class JobExecutor {
public:
  JobExecutor(storage_handoff::StorageWriter &writer, zmq::context_t &zmq_context);

  /**
   * @brief Execute a job definition with the given config and log the outcome.
   * @param job The job definition containing the handler to invoke.
   * @param config The scheduler config JSON for the job to use.
   * @return The result of the job execution.
   */
  JobResult execute(const JobDefinition &job, const nlohmann::json &config);

  /**
   * @brief Push a synthesized command to the command_executor via ZMQ.
   *
   * Used by jobs that need to trigger hardware actions (e.g., treat dispense,
   * lid actuation). The command format matches what CommandDispatcher expects.
   *
   * @param signal_id The signal ID from the command registry.
   * @param signal_type The signal type string.
   * @param payload The command payload JSON.
   * @return true if the command was pushed successfully.
   */
  bool push_command(uint16_t signal_id, const std::string &signal_type,
                    const nlohmann::json &payload);

private:
  storage_handoff::StorageWriter &writer_;
  zmq::socket_t cmd_push_socket_;
};

} // namespace oro::stm

#endif // SCHEDULED_TASK_MANAGER_JOB_EXECUTOR_HPP
