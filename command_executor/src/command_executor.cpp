#include "command_executor/command_executor.hpp"
#include "command_executor/command_handlers.hpp"
#include "command_executor/signal_logger.hpp"
#include <iostream>
#include <zmq.hpp>

namespace oro {

CommandExecutor::CommandExecutor(
    CommandQueue &queue, std::function<void(const Command &)> on_result_cb)
    : queue_(queue), on_result_cb_(on_result_cb) {}

CommandExecutor::~CommandExecutor() { stop(); }

void CommandExecutor::start() {
  running_ = true;
  worker_thread_ = std::thread(&CommandExecutor::run, this);
}

void CommandExecutor::stop() {
  if (running_) {
    running_ = false;
    if (worker_thread_.joinable()) {
      worker_thread_.join();
    }
  }
}

void CommandExecutor::run() {
  std::cout << "[CommandExecutor] Worker thread started.\n";
  CommandHandlers handlers;

  while (running_) {
    auto cmd_opt = queue_.pop(std::chrono::milliseconds(500));
    if (!cmd_opt) {
      continue;
    }

    Command cmd = std::move(*cmd_opt);
    cmd.status = CommandStatus::DISPATCHING;
    std::cout << "[CommandExecutor] Processing signal_id: " << cmd.signal_id
              << " (" << cmd.signal_type << ")\n";

    CommandResult res;
    switch (cmd.signal_id) {
    case 84:
      res = handlers.handle_manual_lid_open(cmd);
      break;
    case 123:
      res = handlers.handle_manual_lid_close(cmd);
      break;
    case 64:
      res = handlers.handle_lid_actuation(cmd);
      break;
    case 85:
      res = handlers.handle_treat_dispense(cmd);
      break;
    case 91:
      res = handlers.handle_photo_capture(cmd);
      break;
    case 88:
      res = handlers.handle_live_session_start(cmd);
      break;
    case 133:
      res = handlers.handle_live_session_end(cmd);
      break;
    case 98:
      res = handlers.handle_settings_apply(cmd);
      break;
    case 134:
      res = handlers.handle_camera_rotation(cmd);
      break;
    case 135:
      res = handlers.handle_video_capture(cmd);
      break;
    case 139:
      res = handlers.handle_video_capture(cmd);
      break;
    case 137:
      res = handlers.handle_play_music(cmd);
      break;
    case 138:
      res = handlers.handle_stop_music(cmd);
      break;
    default:
      std::cerr
          << "[CommandExecutor] Error: No handler registered for signal_id "
          << cmd.signal_id << "\n";
      res.success = false;
      res.failure_reason = "No handler found";
      break;
    }

    if (res.success) {
      cmd.status = CommandStatus::COMPLETED;
      cmd.result = res.data;
    } else {
      cmd.status = CommandStatus::FAILED;
      cmd.result = {{"status", "FAILED"}, {"reason", res.failure_reason}};
    }

    cmd.completed_at = std::chrono::steady_clock::now();

    // Forward back to caller (the responder logic / main ZMQ callback) first to
    // minimize dashboard delay
    if (on_result_cb_) {
      on_result_cb_(cmd);
    }

    SignalLogger::log(cmd);

    // // Perform the database log
    // if (cmd.signal_id == 84 || cmd.signal_id == 123) {
    //   std::cout << "\n";
    // } else {
    //   SignalLogger::log(cmd);
    // }
  }
  std::cout << "[CommandExecutor] Worker thread stopped.\n";
}

} // namespace oro
