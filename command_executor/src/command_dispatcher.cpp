#include "command_executor/command_dispatcher.hpp"
#include "command_executor/command_registry.hpp"
#include <iostream>

namespace oro {

std::optional<Command> CommandDispatcher::parse(const std::string &json_str) {
  try {
    auto j = nlohmann::json::parse(json_str);

    if (!j.contains("header")) {
      std::cerr << "[CommandDispatcher] Error: JSON missing 'header' object\n";
      return std::nullopt;
    }

    auto header = j["header"];
    Command cmd;
    cmd.signal_id = header.value("signal_id", 0);
    cmd.signal_type = header.value("signal_type", "");
    cmd.command_id = header.value("command_id", "");
    cmd.issued_by = header.value("issued_by", "");
    cmd.event_time = header.value("event_time", 0L);
    cmd.status = CommandStatus::RECEIVED;
    cmd.payload = j.value("payload", nlohmann::json::object());
    cmd.received_at = std::chrono::steady_clock::now();

    return cmd;
  } catch (const nlohmann::json::parse_error &e) {
    std::cerr << "[CommandDispatcher] JSON Parse Error: " << e.what() << "\n";
    return std::nullopt;
  }
}

bool CommandDispatcher::validate(Command &cmd) {
  std::lock_guard<std::mutex> lock(mutex_);

  // 1. Check if the signal_id exists and is INBOUND
  bool found_and_valid = false;
  for (const auto &descriptor : COMMAND_REGISTRY) {
    if (descriptor.signal_id == cmd.signal_id) {
      if (descriptor.direction == SignalDirection::INBOUND) {
        found_and_valid = true;
        break;
      } else {
        std::cerr << "[CommandDispatcher] Validation failed: signal_id "
                  << cmd.signal_id << " is an OUTBOUND signal.\n";
        cmd.status = CommandStatus::REJECTED;
        return false;
      }
    }
  }

  if (!found_and_valid) {
    std::cerr << "[CommandDispatcher] Validation failed: unknown or unhandled "
                 "signal_id "
              << cmd.signal_id << "\n";
    cmd.status = CommandStatus::REJECTED;
    return false;
  }

  // 2. Validate essential fields
  if (cmd.command_id.empty() || cmd.issued_by.empty()) {
    std::cerr << "[CommandDispatcher] Validation failed: missing 'command_id' "
                 "or 'issued_by'\n";
    cmd.status = CommandStatus::REJECTED;
    return false;
  }

  // 3. Prevent duplicate command execution
  if (active_command_ids_.find(cmd.command_id) != active_command_ids_.end()) {
    std::cerr << "[CommandDispatcher] Validation failed: duplicate command_id "
              << cmd.command_id << "\n";
    cmd.status = CommandStatus::REJECTED;
    return false;
  }

  active_command_ids_.insert(cmd.command_id);
  cmd.status = CommandStatus::QUEUED;
  return true;
}

} // namespace oro
