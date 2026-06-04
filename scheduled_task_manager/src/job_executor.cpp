#include "scheduled_task_manager/job_executor.hpp"
#include <chrono>
#include <iostream>

namespace oro::stm {

JobExecutor::JobExecutor(storage_handoff::StorageWriter &writer,
                         zmq::context_t &zmq_context)
    : writer_(writer), cmd_push_socket_(zmq_context, zmq::socket_type::push) {
  // Connect to command_executor's PULL socket for dispatching hardware commands
  cmd_push_socket_.connect("ipc:///tmp/oro_cmd_exec.ipc");
  std::cout << "[JobExecutor] Connected to command_executor at "
               "ipc:///tmp/oro_cmd_exec.ipc (PUSH)\n";
}

JobResult JobExecutor::execute(const JobDefinition &job,
                               const nlohmann::json &config) {
  std::cout << "[JobExecutor] Starting job '" << job.name << "' (priority="
            << priority_to_string(job.priority)
            << ", category=" << category_to_string(job.category) << ")\n";

  auto start = std::chrono::steady_clock::now();
  JobResult result;

  try {
    if (job.handler) {
      result = job.handler(config, writer_);
    } else {
      result.success = false;
      result.error = "No handler registered for job";
    }
  } catch (const std::exception &e) {
    result.success = false;
    result.error = std::string("Exception: ") + e.what();
    std::cerr << "[JobExecutor] Job '" << job.name
              << "' threw exception: " << e.what() << "\n";
  }

  auto end = std::chrono::steady_clock::now();
  int duration_ms = static_cast<int>(
      std::chrono::duration_cast<std::chrono::milliseconds>(end - start)
          .count());

  if (result.success) {
    std::cout << "[JobExecutor] Job '" << job.name
              << "' completed successfully in " << duration_ms
              << "ms (items=" << result.items_processed << ")\n";
  } else {
    std::cerr << "[JobExecutor] Job '" << job.name << "' FAILED in "
              << duration_ms << "ms: " << result.error << "\n";
  }

  return result;
}

bool JobExecutor::push_command(uint16_t signal_id,
                               const std::string &signal_type,
                               const nlohmann::json &payload) {
  try {
    // Generate a unique command ID for scheduler-initiated commands
    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::system_clock::now().time_since_epoch())
                      .count();
    std::string command_id =
        "STM_" + std::to_string(signal_id) + "_" + std::to_string(now_ms);

    nlohmann::json cmd_json = {
        {"header",
         {{"signal_id", signal_id},
          {"signal_type", signal_type},
          {"command_id", command_id},
          {"issued_by", "scheduled_task_manager"},
          {"event_time", now_ms}}},
        {"payload", payload}};

    std::string cmd_str = cmd_json.dump();
    zmq::message_t msg(cmd_str.data(), cmd_str.size());
    cmd_push_socket_.send(msg, zmq::send_flags::none);

    std::cout << "[JobExecutor] Pushed command to command_executor: signal_id="
              << signal_id << ", command_id=" << command_id << "\n";
    return true;
  } catch (const std::exception &e) {
    std::cerr << "[JobExecutor] Failed to push command: " << e.what() << "\n";
    return false;
  }
}

} // namespace oro::stm
