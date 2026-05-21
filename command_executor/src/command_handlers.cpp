#include "command_executor/command_handlers.hpp"
#include "data/oro_protocol.hpp"
#include <cstring>
#include <iostream>
#include <nlohmann/json.hpp>
#include <thread>
#include "radxa_drivers/radxa_services.hpp"

namespace oro {

CommandHandlers::CommandHandlers()
    : context_(1), mcu_socket_(context_, zmq::socket_type::req) {
  mcu_socket_.connect("ipc:///tmp/oro_mcu_cmd.ipc");

  int timeout_ms = 6000;
  mcu_socket_.set(zmq::sockopt::rcvtimeo, timeout_ms);
  std::cout
      << "[CommandHandlers] Initialized mcu_socket_ with 6000ms timeout\n";
}

CommandResult CommandHandlers::send_packet_to_mcu(uint8_t peripheral_id,
                                                  int32_t value,
                                                  int timeout_ms) {
  OroPacket pkt{};
  pkt.start = START_BYTE;
  pkt.msg_type = PACK_MSG_TYPE(PRIO_HIGH, MSG_COMMAND);
  pkt.id_seq = PACK_ID_SEQ(0, peripheral_id);
  pack_value_i32(pkt.value, value);
  pkt.crc = oro_crc8(&pkt.msg_type, 6);

  std::cout << "[CommandHandlers] Dispatching packet to MCU: peripheral="
            << (int)peripheral_id << ", value=" << value << "\n";

  zmq::message_t msg(sizeof(OroPacket));
  std::memcpy(msg.data(), &pkt, sizeof(OroPacket));

  CommandResult res;
  try {
    mcu_socket_.set(zmq::sockopt::rcvtimeo, timeout_ms);
    mcu_socket_.send(msg, zmq::send_flags::none);

    zmq::message_t response;
    if (mcu_socket_.recv(response, zmq::recv_flags::none)) {
      std::string resp_str(static_cast<char *>(response.data()),
                           response.size());
      std::cout << "[CommandHandlers] Received response from MCU: " << resp_str
                << "\n";
      try {
        auto resp_json = nlohmann::json::parse(resp_str);
        res.success = true;
        res.data = resp_json;
      } catch (...) {
        res.success = true;
        res.data = {{"status", "success"}, {"raw_response", resp_str}};
      }
    } else {
      std::cout << "[CommandHandlers] MCU REQ timeout, reconstructing socket "
                   "to clear EFSM state.\n";
      mcu_socket_ = zmq::socket_t(context_, zmq::socket_type::req);
      mcu_socket_.connect("ipc:///tmp/oro_mcu_cmd.ipc");
      mcu_socket_.set(zmq::sockopt::rcvtimeo, timeout_ms);

      res.success = true;
      res.data = {{"status", "success"},
                  {"completion_time",
                   std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::system_clock::now().time_since_epoch())
                       .count()},
                  {"mcu_timeout", true}};
    }
  } catch (const std::exception &e) {
    std::cerr << "[CommandHandlers] ZMQ error: " << e.what() << "\n";
    std::cout << "[CommandHandlers] Reconstructing socket to clear EFSM state "
                 "after ZMQ error.\n";
    mcu_socket_ = zmq::socket_t(context_, zmq::socket_type::req);
    mcu_socket_.connect("ipc:///tmp/oro_mcu_cmd.ipc");
    mcu_socket_.set(zmq::sockopt::rcvtimeo, timeout_ms);

    res.success = false;
    res.data = {{"status", "failed"},
                {"completion_time",
                 std::chrono::duration_cast<std::chrono::milliseconds>(
                     std::chrono::system_clock::now().time_since_epoch())
                     .count()},
                {"mcu_error", e.what()}};
  }
  return res;
}

CommandResult CommandHandlers::handle_manual_lid_open(Command &cmd) {
  std::cout << "[CommandHandlers] Logging passive manual_lid_open_command_event ("
            << cmd.command_id << ")\n";

  // TODO: Write this detected manual_lid_open_command_event to the database tables.
  // The event should be stored in the 'signals' and/or 'commands' table with relevant metadata.

  CommandResult res;
  res.success = true;
  res.data = {{"status", "success"},
              {"completion_time",
               std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::system_clock::now().time_since_epoch())
                   .count()}};
  return res;
}

CommandResult CommandHandlers::handle_manual_lid_close(Command &cmd) {
  std::cout << "[CommandHandlers] Logging passive manual_lid_close_command_event ("
            << cmd.command_id << ")\n";

  // TODO: Write this detected manual_lid_close_command_event to the database tables.
  // The event should be stored in the 'signals' and/or 'commands' table with relevant metadata.

  CommandResult res;
  res.success = true;
  res.data = {{"status", "success"},
              {"completion_time",
               std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::system_clock::now().time_since_epoch())
                   .count()}};
  return res;
}

CommandResult CommandHandlers::handle_lid_actuation(Command &cmd) {
  std::cout << "[CommandHandlers] Executing lid_actuation_command ("
            << cmd.command_id << ")\n";

  uint8_t lid_id = 1;
  if (cmd.payload.contains("lid_id")) {
    lid_id = cmd.payload["lid_id"].get<uint8_t>();
  }

  uint8_t pid = PID_LID1_STEPPER;
  if (lid_id == 2) {
    pid = PID_LID2_STEPPER;
  }

  int32_t val = 1;
  int default_timeout = 10000;

  if (cmd.payload.contains("action")) {
    int8_t action_val = 0;
    if (cmd.payload["action"].is_number()) {
        action_val = cmd.payload["action"].get<int8_t>();
    } else if (cmd.payload["action"].is_string()) {
        std::string act_str = cmd.payload["action"].get<std::string>();
        action_val = (act_str == "open" || act_str == "1") ? 1 : 0;
    }
    
    val = (action_val != 0) ? 1 : 0;
    
    // Transform payload value to string for more descriptive database logging
    cmd.payload["action"] = (val == 1) ? "open" : "close";
  }

  int timeout_ms = default_timeout;
  if (cmd.payload.contains("timeout_ms")) {
    timeout_ms = cmd.payload["timeout_ms"].get<int>();
  }

  auto res = send_packet_to_mcu(pid, val, timeout_ms);
  
  // Enrich the result with context about the original command
  res.data["command_id"] = cmd.command_id;
  res.data["action"] = cmd.payload.value("action", (val == 1 ? "open" : "close"));
  res.data["lid_id"] = lid_id;
  res.data["completion_time"] = std::chrono::duration_cast<std::chrono::milliseconds>(
                                    std::chrono::system_clock::now().time_since_epoch())
                                    .count();
  res.data["failure_reason"] = res.success ? "" : res.failure_reason;
  
  return res;
}

CommandResult CommandHandlers::handle_treat_dispense(Command &cmd) {
  std::cout << "[CommandHandlers] Executing treat_dispense_command_event ("
            << cmd.command_id << ")\n";
  
  double quantity = 1.0;
  if (cmd.payload.contains("treat_quantity")) {
    quantity = cmd.payload["treat_quantity"].get<double>();
  } else if (cmd.payload.contains("value")) {
    quantity = cmd.payload["value"].get<double>();
  }

  // Delegate to RadxaServices
  nlohmann::json radxa_cmd = {
    {"topic", "/commands/treat/dispense"},
    {"value", static_cast<float>(quantity)}
  };
  std::string resp_str = RadxaServices::process_command(radxa_cmd);

  CommandResult res;
  try {
    auto resp_json = nlohmann::json::parse(resp_str);
    res.success = (resp_json.value("status", "") == "success");
    res.data = resp_json;
  } catch (...) {
    res.success = false;
    res.data = {{"status", "failed"}, {"error", "parse_error"}};
  }
  
  res.data["command_id"] = cmd.command_id;
  res.data["dispenser_id"] = 1; // Default dispenser - Oro base contains only 1 dispenser.
  res.data["event_time"] = cmd.event_time;
  if (!res.data.contains("treats_dispensed")) {
      res.data["treats_dispensed"] = quantity;
  }

  return res;
}

CommandResult CommandHandlers::handle_photo_capture(Command &cmd) {
  std::cout << "[CommandHandlers] Executing photo_capture_command_event ("
            << cmd.command_id << ")\n";
  
  // Delegate to RadxaServices
  nlohmann::json radxa_cmd = {
    {"topic", "/commands/photo_capture"},
    {"value", 1.0f}
  };
  std::string resp_str = RadxaServices::process_command(radxa_cmd);
  
  CommandResult res;
  try {
    auto resp_json = nlohmann::json::parse(resp_str);
    res.success = (resp_json.value("status", "") == "success");
    res.data = resp_json;
  } catch (...) {
    res.success = false;
    res.data = {{"status", "failed"}, {"error", "parse_error"}};
  }
  
  res.data["command_id"] = cmd.command_id;
  res.data["event_time"] = cmd.event_time;
  // Signal #93 requirement: add file_id and storage_path if they exist, or placeholders
  if (!res.data.contains("saved")) {
    res.data["saved"] = res.success;
  }
  if (!res.data.contains("file_id")) {
    res.data["file_id"] = "IMG_" + cmd.command_id;
  }
  if (!res.data.contains("storage_path")) {
    res.data["storage_path"] = "/home/radxa/Pictures/Command_Executor_Images/ORoBase_IMG_" + cmd.command_id + ".jpg";
  }

  return res;
}

CommandResult CommandHandlers::handle_live_session_start(Command &cmd) {
  std::cout << "[CommandHandlers] Executing live_session_start_event ("
            << cmd.command_id << ")\n";

  // Delegate to RadxaServices
  nlohmann::json radxa_cmd = {
    {"topic", "/commands/live_session/start"},
    {"value", 1.0f}
  };
  std::string resp_str = RadxaServices::process_command(radxa_cmd);

  CommandResult res;
  try {
    auto resp_json = nlohmann::json::parse(resp_str);
    res.success = (resp_json.value("status", "") == "success");
    res.data = resp_json;
  } catch (...) {
    res.success = false;
    res.data = {{"status", "failed"}, {"error", "parse_error"}};
  }
  
  // Enrich with metadata for database logging
  current_session_id_ = cmd.command_id; // Store for end event
  res.data["session_id"] = current_session_id_;
  res.data["initiated_by"] = cmd.issued_by;
  res.data["event_time"] = cmd.event_time;
  res.data["command_id"] = cmd.command_id;
  
  return res;
}

CommandResult CommandHandlers::handle_live_session_end(Command &cmd) {
  std::cout << "[CommandHandlers] Executing live_session_end_event ("
            << cmd.command_id << ")\n";

  // Delegate to RadxaServices
  nlohmann::json radxa_cmd = {
    {"topic", "/commands/live_session/end"},
    {"value", 1.0f}
  };
  std::string resp_str = RadxaServices::process_command(radxa_cmd);

  CommandResult res;
  try {
    auto resp_json = nlohmann::json::parse(resp_str);
    res.success = (resp_json.value("status", "") == "success");
    res.data = resp_json;
  } catch (...) {
    res.success = false;
    res.data = {{"status", "failed"}, {"error", "parse_error"}};
  }
  
  // Enrich with metadata for database logging
  // Use the stored session_id if available, otherwise fallback to command payload/id
  if (!current_session_id_.empty()) {
      res.data["session_id"] = current_session_id_;
      current_session_id_ = ""; // Clear session
  } else {
      res.data["session_id"] = cmd.payload.value("session_id", cmd.command_id);
  }
  
  res.data["initiated_by"] = cmd.issued_by;
  res.data["event_time"] = cmd.event_time;
  res.data["command_id"] = cmd.command_id;
  
  return res;
}

CommandResult CommandHandlers::handle_settings_apply(Command &cmd) {
  std::cout << "[CommandHandlers] Executing settings_apply_success_status ("
            << cmd.command_id << ")\n";

  // Delegate to RadxaServices
  // We pass the whole command payload to handle_apply_settings
  std::string resp_str = RadxaServices::process_command(cmd.payload);

  CommandResult res;
  try {
    auto resp_json = nlohmann::json::parse(resp_str);
    res.success = (resp_json.value("status", "") == "success");
    res.data = resp_json;
  } catch (...) {
    res.success = false;
    res.data = {{"status", "failed"}, {"error", "parse_error"}};
  }

  // Enrich with metadata for database logging
  res.data["command_id"] = cmd.command_id;
  res.data["event_time"] = cmd.event_time;

  return res;
}

} // namespace oro
