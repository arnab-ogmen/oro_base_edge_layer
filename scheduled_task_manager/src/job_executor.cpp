#include "scheduled_task_manager/job_executor.hpp"
#include <chrono>
#include <iostream>

namespace oro::stm {

JobExecutor::JobExecutor(storage_handoff::StorageWriter& writer, LockManager& lock_manager, std::string owner_id)
    : writer_(writer), lock_manager_(lock_manager), owner_id_(owner_id),
      zmq_context_(1), cmd_push_socket_(zmq_context_, zmq::socket_type::push) {
    
    // Connect to command_executor PULL socket for hardware command dispatch
    cmd_push_socket_.connect("ipc:///tmp/oro_cmd_exec.ipc");
    std::cout << "[JobExecutor] Connected to command_executor at ipc:///tmp/oro_cmd_exec.ipc (PUSH)\n";
}

void JobExecutor::prepare_statements() {
    writer_.prepare("stm_log_execution", R"(
        INSERT INTO stm_job_executions (
            job_name, status, started_at, duration_ms, items_processed, error, metadata, owner
        ) VALUES ($1, $2, $3::timestamptz, $4, $5, $6, $7, $8)
    )");
}

JobResult JobExecutor::execute(const JobDefinition& job, const nlohmann::json& config) {
    auto start_tp = std::chrono::system_clock::now();
    uint64_t start_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            start_tp.time_since_epoch())
                            .count();
    std::string start_iso = storage_handoff::StorageWriter::unix_ms_to_iso8601(start_ms);

    // 1. Try to acquire database-backed lock
    // Lock TTL is timeout_seconds plus a safety buffer of 30 seconds
    int lock_ttl = job.timeout_seconds + 30;
    if (!lock_manager_.try_acquire(job.name, lock_ttl)) {
        // Log as 'skipped' since lock is held by another worker
        log_execution(job.name, "skipped", start_iso, 0, 0, "Lock busy", nlohmann::json::object());
        JobResult skipped_res;
        skipped_res.success = false;
        skipped_res.error = "Lock busy";
        return skipped_res;
    }

    std::cout << "[JobExecutor] Executing job '" << job.name << "'...\n";
    JobResult result;
    auto execute_start = std::chrono::steady_clock::now();

    try {
        if (job.handler) {
            result = job.handler(config, writer_);
        } else {
            result.success = false;
            result.error = "No handler registered";
        }
    } catch (const std::exception& e) {
        result.success = false;
        result.error = std::string("Exception: ") + e.what();
    }

    auto execute_end = std::chrono::steady_clock::now();
    int duration_ms = static_cast<int>(
        std::chrono::duration_cast<std::chrono::milliseconds>(execute_end - execute_start)
            .count());

    // 2. Log execution details to DB
    std::string status = result.success ? "completed" : "failed";
    log_execution(job.name, status, start_iso, duration_ms, result.items_processed, result.error, result.metadata);

    // 3. Release lock
    lock_manager_.release(job.name);

    if (result.success) {
        std::cout << "[JobExecutor] Job '" << job.name << "' completed successfully in " << duration_ms << "ms\n";
    } else {
        std::cerr << "[JobExecutor] Job '" << job.name << "' failed in " << duration_ms << "ms: " << result.error << "\n";
    }

    return result;
}

void JobExecutor::log_execution(const std::string& job_name, const std::string& status,
                               const std::string& started_at, int duration_ms,
                               int items_processed, const std::string& error_msg,
                               const nlohmann::json& metadata) {
    try {
        std::string dur_str = std::to_string(duration_ms);
        std::string items_str = std::to_string(items_processed);
        std::string meta_str = metadata.dump();

        writer_.execute_prepared("stm_log_execution",
                                 job_name,
                                 status,
                                 started_at,
                                 dur_str,
                                 items_str,
                                 error_msg.empty() ? nullptr : error_msg.c_str(),
                                 meta_str,
                                 owner_id_);
    } catch (const std::exception& e) {
        std::cerr << "[JobExecutor] Failed to write audit log to database: " << e.what() << "\n";
    }
}

bool JobExecutor::push_command(uint16_t signal_id, const std::string& signal_type, const nlohmann::json& payload) {
    try {
        auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::system_clock::now().time_since_epoch())
                          .count();
        std::string command_id = "STM_" + std::to_string(signal_id) + "_" + std::to_string(now_ms);

        nlohmann::json cmd_json = {
            {"header", {
                {"signal_id", signal_id},
                {"signal_type", signal_type},
                {"command_id", command_id},
                {"issued_by", "scheduled_task_manager"},
                {"event_time", now_ms}
            }},
            {"payload", payload}
        };

        std::string cmd_str = cmd_json.dump();
        zmq::message_t msg(cmd_str.data(), cmd_str.size());
        cmd_push_socket_.send(msg, zmq::send_flags::none);

        std::cout << "[JobExecutor] Pushed command to command_executor: signal_id="
                  << signal_id << ", command_id=" << command_id << "\n";
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[JobExecutor] Failed to push ZMQ command: " << e.what() << "\n";
        return false;
    }
}

} // namespace oro::stm
