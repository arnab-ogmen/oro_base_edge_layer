#include "command_executor/command_handlers.hpp"
#include "data/oro_protocol.hpp"
#include "command_executor/signal_logger.hpp"
#include <cstring>
#include <iostream>
#include <nlohmann/json.hpp>
#include <thread>
#include <cstdlib>
#include "radxa_drivers/radxa_services.hpp"

namespace oro {
namespace {

constexpr const char *kDefaultDeviceId = "9e092b69-5973-46e4-a228-fe4933e04364";

std::string resolve_device_id(const Command &cmd) {
  if (cmd.payload.contains("device_id") &&
      cmd.payload["device_id"].is_string()) {
    return cmd.payload["device_id"].get<std::string>();
  }
  if (const char *env_device_id = std::getenv("ORO_DEVICE_ID")) {
    return std::string(env_device_id);
  }
  return kDefaultDeviceId;
}

std::optional<std::string> resolve_dog_id(const Command &cmd) {
  if (cmd.payload.contains("dog_id") && cmd.payload["dog_id"].is_string()) {
    return cmd.payload["dog_id"].get<std::string>();
  }
  return std::nullopt;
}

// Send a camera rotation command to the Radxa-local GPIO stepper motor.
// Despite routing through ipc:///tmp/oro_mcu_cmd.ipc, the McuSerialReaderNode
// intercepts PID_CAMERA_STEPPER packets and drives the stepper directly via
// GPIO (gpiochip1/gpiochip0) rather than forwarding them to the MCU over UART.
// The ZMQ REP returns an immediate ACK — the physical motion continues
// asynchronously in McuSerialReaderNode::stepper_thread_func.
//
// val_hundreds: target angle * 100  (e.g., 9000 = +90°, -9000 = -90°)
//              Special value 99900 triggers the homing/calibration sequence.
bool send_stepper_command(int32_t val_hundreds) {
  try {
    zmq::context_t ctx(1);
    zmq::socket_t sock(ctx, zmq::socket_type::req);
    sock.connect("ipc:///tmp/oro_mcu_cmd.ipc");
    
    sock.set(zmq::sockopt::rcvtimeo, 15000);
    sock.set(zmq::sockopt::sndtimeo, 15000);

    OroPacket pkt{};
    pkt.start = START_BYTE;
    pkt.msg_type = PACK_MSG_TYPE(PRIO_HIGH, MSG_COMMAND);
    pkt.id_seq = PACK_ID_SEQ(0, PID_CAMERA_STEPPER);
    pack_value_i32(pkt.value, val_hundreds);
    pkt.crc = oro_crc8(&pkt.msg_type, 6);

    zmq::message_t req_msg(sizeof(OroPacket));
    std::memcpy(req_msg.data(), &pkt, sizeof(OroPacket));

    std::cout << "[CommandHandlers] Sending stepper command to Radxa GPIO stepper: " << val_hundreds << "\n";
    sock.send(req_msg, zmq::send_flags::none);

    zmq::message_t resp_msg;
    if (sock.recv(resp_msg, zmq::recv_flags::none)) {
      std::string resp_str(static_cast<char*>(resp_msg.data()), resp_msg.size());
      std::cout << "[CommandHandlers] Stepper ACK received: " << resp_str << "\n";
      return true;
    } else {
      std::cerr << "[CommandHandlers] Stepper command timeout (no ACK received)\n";
      return false;
    }
  } catch (const std::exception &e) {
    std::cerr << "[CommandHandlers] Exception sending stepper command: " << e.what() << "\n";
    return false;
  }
}

// Wait for the Radxa GPIO stepper motor to finish its current motion.
// Subscribes to the stepper status ZMQ topic published by McuSerialReaderNode
// and waits until the motor transitions from running (1) to idle (0).
// Returns true if the stepper became idle, false on timeout.
bool wait_for_stepper_idle(int timeout_sec = 20) {
  try {
    zmq::context_t ctx(1);
    zmq::socket_t sub(ctx, zmq::socket_type::sub);
    sub.connect("ipc:///tmp/oro_status.ipc");
    sub.set(zmq::sockopt::subscribe, "/status/camera_rotation/stepper_motor");
    sub.set(zmq::sockopt::rcvtimeo, 1000);

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeout_sec);
    bool saw_running = false;

    while (std::chrono::steady_clock::now() < deadline) {
      zmq::message_t topic_msg, data_msg;

      auto res = sub.recv(topic_msg, zmq::recv_flags::none);
      if (!res) continue;

      res = sub.recv(data_msg, zmq::recv_flags::none);
      if (!res) continue;

      // The payload is a DigitalPayload struct — the 'state' field indicates
      // 1 = motor running, 0 = motor idle
      if (data_msg.size() >= sizeof(uint64_t) + sizeof(uint8_t) * 2 + sizeof(uint8_t)) {
        // DigitalPayload: { MsgHeader(12 bytes), uint8_t state }
        // MsgHeader: { uint8_t sensor_id, uint8_t seq_num, uint64_t timestamp_ms }
        const uint8_t *raw = static_cast<const uint8_t*>(data_msg.data());
        // state is at offset 10 (after sensor_id(1) + seq_num(1) + timestamp_ms(8))
        uint8_t state = raw[10];

        if (state == 1) {
          saw_running = true;
        } else if (state == 0 && saw_running) {
          std::cout << "[CommandHandlers] Stepper motor reached idle after motion\n";
          return true;
        }
      }
    }

    std::cerr << "[CommandHandlers] Stepper wait timeout (" << timeout_sec << "s)\n";
    return false;
  } catch (const std::exception &e) {
    std::cerr << "[CommandHandlers] Exception waiting for stepper idle: " << e.what() << "\n";
    return false;
  }
}

// Wait for a camera rotation limit switch to be pressed.
// Subscribes to the limit switch ZMQ topic published by McuSerialReaderNode
// and waits until the limit switch state becomes active (non-zero).
// Returns true if the switch became active, false on timeout.
bool wait_for_limit_switch(const std::string &topic, int timeout_sec = 25) {
  try {
    zmq::context_t ctx(1);
    zmq::socket_t sub(ctx, zmq::socket_type::sub);
    sub.connect("ipc:///tmp/oro_sensors.ipc");
    sub.set(zmq::sockopt::subscribe, topic);
    sub.set(zmq::sockopt::rcvtimeo, 500);

    std::cout << "[CommandHandlers] Waiting for limit switch on topic: " << topic << "...\n";

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeout_sec);
    while (std::chrono::steady_clock::now() < deadline) {
      zmq::message_t topic_msg, data_msg;

      auto res = sub.recv(topic_msg, zmq::recv_flags::none);
      if (!res) continue;

      res = sub.recv(data_msg, zmq::recv_flags::none);
      if (!res) continue;

      if (data_msg.size() >= sizeof(uint64_t) + sizeof(uint8_t) * 2 + sizeof(uint8_t)) {
        const uint8_t *raw = static_cast<const uint8_t*>(data_msg.data());
        uint8_t state = raw[10];

        if (state != 0) {
          std::cout << "[CommandHandlers] Limit switch on " << topic << " pressed!\n";
          return true;
        }
      }
    }

    std::cerr << "[CommandHandlers] Limit switch wait timeout (" << timeout_sec << "s) for " << topic << "\n";
    return false;
  } catch (const std::exception &e) {
    std::cerr << "[CommandHandlers] Exception waiting for limit switch: " << e.what() << "\n";
    return false;
  }
}

} // namespace


CommandHandlers::CommandHandlers()
    : context_(1), mcu_socket_(context_, zmq::socket_type::req), privacy_mode_enabled_(false) {
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

  int speed = 3; // Default speed
  if (cmd.payload.contains("treat_speed")) {
    speed = cmd.payload["treat_speed"].get<int>();
  } else if (cmd.payload.contains("speed")) {
    speed = cmd.payload["speed"].get<int>();
  }

  // Delegate to RadxaServices
  nlohmann::json radxa_cmd = {
    {"topic", "/commands/treat/dispense"},
    {"value", static_cast<float>(quantity)},
    {"speed", speed}
  };
  std::string resp_str = RadxaServices::process_command(radxa_cmd);

  CommandResult res;
  try {
    auto resp_json = nlohmann::json::parse(resp_str);
    res.success = (resp_json.value("status", "") == "success");
    res.data = resp_json;
    if (!res.success) {
      res.failure_reason = resp_json.value("error", "unknown_dispense_error");
    }
  } catch (...) {
    res.success = false;
    res.data = {{"status", "failed"}, {"error", "parse_error"}};
    res.failure_reason = "parse_error";
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
  
  if (privacy_mode_enabled_) {
    CommandResult res;
    res.success = false;
    res.failure_reason = "privacy_mode_active";
    res.data = {
      {"status", "failed"},
      {"error", "privacy_mode_active"},
      {"command_id", cmd.command_id},
      {"event_time", cmd.event_time}
    };
    return res;
  }
  
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
    res.data["storage_path"] = "/home/radxa/oro_base_images/ORoBase_IMG_" + cmd.command_id + ".jpg";
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

CommandResult CommandHandlers::handle_camera_rotation(Command &cmd) {
  std::cout << "[CommandHandlers] Executing camera_rotation_command ("
            << cmd.command_id << ")\n";

  if (cmd.payload.contains("action") && cmd.payload["action"].is_string() && cmd.payload["action"].get<std::string>() == "home") {
    std::cout << "[CommandHandlers] Homing requested at runtime!\n";
    int timeout_ms = 15000;
    auto res = send_packet_to_mcu(PID_CAMERA_STEPPER, 99900, timeout_ms);
    if (res.success) {
      current_camera_angle_ = 0.0f;
    }
    res.data["command_id"] = cmd.command_id;
    res.data["angle"] = 0.0f;
    res.data["completion_time"] = std::chrono::duration_cast<std::chrono::milliseconds>(
                                      std::chrono::system_clock::now().time_since_epoch())
                                      .count();
    res.data["failure_reason"] = res.success ? "" : res.failure_reason;
    return res;
  }

  float angle = 0.0f;
  if (cmd.payload.contains("angle")) {
    if (cmd.payload["angle"].is_number()) {
      angle = cmd.payload["angle"].get<float>();
    } else if (cmd.payload["angle"].is_string()) {
      angle = std::stof(cmd.payload["angle"].get<std::string>());
    }
  } else if (cmd.payload.contains("value")) {
    if (cmd.payload["value"].is_number()) {
      angle = cmd.payload["value"].get<float>();
    } else if (cmd.payload["value"].is_string()) {
      angle = std::stof(cmd.payload["value"].get<std::string>());
    }
  }

  int direction = 0;
  bool has_direction = false;
  if (cmd.payload.contains("direction")) {
    has_direction = true;
    if (cmd.payload["direction"].is_number()) {
      direction = cmd.payload["direction"].get<int>();
    } else if (cmd.payload["direction"].is_string()) {
      direction = std::stoi(cmd.payload["direction"].get<std::string>());
    }
  }

  float target_angle = angle;
  if (has_direction) {
    if (direction == 1) {
      target_angle = current_camera_angle_ + angle;
    } else if (direction == -1) {
      target_angle = -angle;
    }
  }

  // Constrain target angle to safe bounds [-90.0f, 90.0f]
  if (target_angle < -90.0f) target_angle = -90.0f;
  if (target_angle > 90.0f) target_angle = 90.0f;

  std::cout << "[CommandHandlers] Current angle=" << current_camera_angle_
            << ", input angle=" << angle << ", direction=" << direction
            << ", target angle=" << target_angle << "\n";

  // Convert to fixed-point fixed-point value * 100 for the protocol payload
  int32_t val_hundreds = static_cast<int32_t>(target_angle * 100.0f);

  int timeout_ms = 15000; // 15s timeout to allow full calibration/movement
  if (cmd.payload.contains("timeout_ms")) {
    timeout_ms = cmd.payload["timeout_ms"].get<int>();
  }

  auto res = send_packet_to_mcu(PID_CAMERA_STEPPER, val_hundreds, timeout_ms);

  if (res.success) {
    current_camera_angle_ = target_angle;
  }

  // Enrich with metadata for database logging
  res.data["command_id"] = cmd.command_id;
  res.data["angle"] = target_angle;
  res.data["completion_time"] = std::chrono::duration_cast<std::chrono::milliseconds>(
                                    std::chrono::system_clock::now().time_since_epoch())
                                    .count();
  res.data["failure_reason"] = res.success ? "" : res.failure_reason;

  return res;
}

CommandResult CommandHandlers::handle_video_capture(Command &cmd) {
  std::cout << "[CommandHandlers] Initiating non-blocking video command ("
            << cmd.command_id << ")\n";

  CommandResult res;
  res.success = true;

  if (privacy_mode_enabled_) {
    res.success = false;
    res.failure_reason = "privacy_mode_active";
    res.data = {
      {"status", "failed"},
      {"error", "privacy_mode_active"},
      {"command_id", cmd.command_id},
      {"event_time", cmd.event_time}
    };
    return res;
  }

  std::string device_id = resolve_device_id(cmd);
  std::optional<std::string> dog_id = resolve_dog_id(cmd);

  bool is_continuous_start = false;
  bool is_continuous_stop = false;
  bool is_continuous = false;

  if (cmd.payload.contains("action")) {
    is_continuous = true;
    if (cmd.payload["action"].is_string()) {
      std::string act = cmd.payload["action"].get<std::string>();
      if (act == "start") {
        is_continuous_start = true;
      } else if (act == "stop") {
        is_continuous_stop = true;
      } else {
        res.success = false;
        res.failure_reason = "invalid_action_value: " + act;
        return res;
      }
    } else {
      res.success = false;
      res.failure_reason = "invalid_action_type: action must be a string";
      return res;
    }
  }

  // If not continuous, validate optional duration for legacy recording
  if (!is_continuous) {
    if (cmd.payload.contains("duration")) {
      if (!cmd.payload["duration"].is_number_integer()) {
        res.success = false;
        res.failure_reason = "invalid_duration_type: duration must be an integer";
        return res;
      }
      int duration_sec = cmd.payload["duration"].get<int>();
      if (duration_sec <= 0) {
        res.success = false;
        res.failure_reason = "invalid_duration_value: duration must be greater than 0";
        return res;
      }
    }
  }

  // If stopping continuous recording, ensure session is active
  if (is_continuous_stop) {
    std::lock_guard<std::mutex> lock(continuous_mutex_);
    if (continuous_storage_path_.empty()) {
      res.success = false;
      res.failure_reason = "no_active_continuous_recording";
      return res;
    }
  }

  std::string msg = "video_capture_initiated";
  if (is_continuous_start) {
    msg = "continuous_recording_started";
    std::thread([this, device_id, dog_id, cmd]() {
      std::string storage_path;
      {
        std::lock_guard<std::mutex> lock(continuous_mutex_);
        // 1. Terminate any existing recording session first
        std::system("pkill -INT -f 'video_recorder.py'");
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        int64_t timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        std::string filename = "ORoBase_VID_" + std::to_string(timestamp) + ".mp4";
        continuous_storage_path_ = "/home/radxa/oro_base_video_audio/" + filename;
        continuous_file_id_ = "VID_" + cmd.command_id;
        continuous_command_id_ = cmd.command_id;
        continuous_device_id_ = device_id;
        continuous_dog_id_ = dog_id;
        
        storage_path = continuous_storage_path_;
      }

      // Ensure target directory exists
      std::system("mkdir -p /home/radxa/oro_base_video_audio");

      // Start video_recorder.py in the background
      std::string record_cmd = "python3 /home/radxa/oro_base/oro_base_input_layer/scripts/video_recorder.py --output " + storage_path + " > /dev/null 2>&1 &";
      std::cout << "[CommandHandlers] Starting continuous video recorder: " << record_cmd << "\n";
      std::system(record_cmd.c_str());

      // Start a watcher thread to enforce the 30-second maximum cap
      std::thread([this, cmd_id = cmd.command_id]() {
        std::this_thread::sleep_for(std::chrono::seconds(30));
        
        std::string storage_path_to_upload;
        std::string file_id_to_upload;
        std::string cmd_id_to_upload;
        
        {
          std::lock_guard<std::mutex> lock(continuous_mutex_);
          if (!continuous_storage_path_.empty() && continuous_command_id_ == cmd_id) {
            std::cout << "[CommandHandlers] Continuous recording exceeded 30s cap. Auto-stopping...\n";
            
            // Gracefully stop continuous recording
            std::system("pkill -INT -f 'video_recorder.py'");
            
            storage_path_to_upload = continuous_storage_path_;
            file_id_to_upload = continuous_file_id_;
            cmd_id_to_upload = continuous_command_id_;
            
            // Reset paths under lock
            continuous_storage_path_ = "";
            continuous_file_id_ = "";
            continuous_command_id_ = "";
            continuous_device_id_ = "";
            continuous_dog_id_ = std::nullopt;
          }
        }

        if (!storage_path_to_upload.empty()) {
          // Wait for file to finalize outside the lock
          std::this_thread::sleep_for(std::chrono::seconds(2));

          // Upload video
          std::string upload_cmd = "python3 /home/radxa/oro_base/oro_base_input_layer/scripts/oro_cloud_bridge.py --upload " + storage_path_to_upload + " --type video > /dev/null 2>&1";
          std::cout << "[CommandHandlers] Uploading auto-stopped continuous video: " << upload_cmd << "\n";
          int upload_ret = std::system(upload_cmd.c_str());
          bool success = (upload_ret == 0);

          auto now = std::chrono::system_clock::now();
          int64_t confirmed_at = std::chrono::duration_cast<std::chrono::milliseconds>(
              now.time_since_epoch()).count();

          nlohmann::json confirm_payload = {
              {"file_id", file_id_to_upload},
              {"storage_path", storage_path_to_upload},
              {"status", success ? "success" : "failed"}
          };

          Command confirm_cmd;
          confirm_cmd.signal_id = 136;
          confirm_cmd.signal_type = "video_file_save_confirmation";
          confirm_cmd.command_id = cmd_id_to_upload;
          confirm_cmd.issued_by = "system";
          confirm_cmd.event_time = confirmed_at;
          confirm_cmd.payload = confirm_payload;

          SignalLogger::log(confirm_cmd);

          std::cout << "[CommandHandlers] Auto-stopped continuous video save confirmation logged. Success: " << success << "\n";
        }
      }).detach();
    }).detach();
  } else if (is_continuous_stop) {
    msg = "continuous_recording_stopped";
    std::thread([this, cmd]() {
      std::string storage_path_to_upload;
      std::string file_id_to_upload;
      std::string cmd_id_to_upload;

      {
        std::lock_guard<std::mutex> lock(continuous_mutex_);
        // 1. Gracefully stop continuous recording
        std::system("pkill -INT -f 'video_recorder.py'");

        if (continuous_storage_path_.empty()) {
          std::cerr << "[CommandHandlers] Stop requested but no active continuous recording path found.\n";
          return;
        }

        storage_path_to_upload = continuous_storage_path_;
        file_id_to_upload = continuous_file_id_;
        cmd_id_to_upload = continuous_command_id_;

        // Reset paths under lock
        continuous_storage_path_ = "";
        continuous_file_id_ = "";
        continuous_command_id_ = "";
        continuous_device_id_ = "";
        continuous_dog_id_ = std::nullopt;
      }

      // 2. Wait for file to finalize and upload outside the lock
      std::this_thread::sleep_for(std::chrono::seconds(2));

      std::string upload_cmd = "python3 /home/radxa/oro_base/oro_base_input_layer/scripts/oro_cloud_bridge.py --upload " + storage_path_to_upload + " --type video > /dev/null 2>&1";
      std::cout << "[CommandHandlers] Uploading continuous video: " << upload_cmd << "\n";
      int upload_ret = std::system(upload_cmd.c_str());
      bool success = (upload_ret == 0);

      auto now = std::chrono::system_clock::now();
      int64_t confirmed_at = std::chrono::duration_cast<std::chrono::milliseconds>(
          now.time_since_epoch()).count();

      nlohmann::json confirm_payload = {
          {"file_id", file_id_to_upload},
          {"storage_path", storage_path_to_upload},
          {"status", success ? "success" : "failed"}
      };

      Command confirm_cmd;
      confirm_cmd.signal_id = 136;
      confirm_cmd.signal_type = "video_file_save_confirmation";
      confirm_cmd.command_id = cmd_id_to_upload;
      confirm_cmd.issued_by = "system";
      confirm_cmd.event_time = confirmed_at;
      confirm_cmd.payload = confirm_payload;

      SignalLogger::log(confirm_cmd);

      std::cout << "[CommandHandlers] Continuous video save confirmation logged. Success: " << success << "\n";
    }).detach();
  } else {
    // Legacy video recording with explicit duration
    std::thread([device_id, dog_id, cmd]() {
      int duration_sec = 10;
      if (cmd.payload.contains("duration")) {
        duration_sec = cmd.payload["duration"].get<int>();
      }
      if (duration_sec > 30) {
        duration_sec = 30; // Cap at 30 seconds max!
      }

      int64_t timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch()).count();
      std::string filename = "ORoBase_VID_" + std::to_string(timestamp) + ".mp4";
      std::string storage_path = "/home/radxa/oro_base_video_audio/" + filename;
      std::string file_id = "VID_" + cmd.command_id;

      // Ensure target directory exists
      std::system("mkdir -p /home/radxa/oro_base_video_audio");

      // Terminate any running instances of video_recorder.py
      std::system("pkill -INT -f 'video_recorder.py'");
      std::this_thread::sleep_for(std::chrono::milliseconds(500));

      // Start video_recorder.py in the background
      std::string record_cmd = "python3 /home/radxa/oro_base/oro_base_input_layer/scripts/video_recorder.py --output " + storage_path + " > /dev/null 2>&1 &";
      std::cout << "[CommandHandlers] Starting video recorder for duration " << duration_sec << "s: " << record_cmd << "\n";
      std::system(record_cmd.c_str());

      // Sleep for the recording duration
      std::this_thread::sleep_for(std::chrono::seconds(duration_sec));

      // Gracefully stop the recording
      std::system("pkill -INT -f 'video_recorder.py'");
      std::this_thread::sleep_for(std::chrono::seconds(2)); // wait for trailer flush

      // Upload video
      std::string upload_cmd = "python3 /home/radxa/oro_base/oro_base_input_layer/scripts/oro_cloud_bridge.py --upload " + storage_path + " --type video > /dev/null 2>&1";
      std::cout << "[CommandHandlers] Uploading recorded video via Python CLI: " << upload_cmd << "\n";
      int upload_ret = std::system(upload_cmd.c_str());
      std::cout << "[CommandHandlers] Python CLI Upload completed with return code: " << upload_ret << "\n";
      bool success = (upload_ret == 0);

      auto now = std::chrono::system_clock::now();
      int64_t confirmed_at = std::chrono::duration_cast<std::chrono::milliseconds>(
          now.time_since_epoch()).count();

      nlohmann::json confirm_payload = {
          {"file_id", file_id},
          {"storage_path", storage_path},
          {"status", success ? "success" : "failed"}
      };

      Command confirm_cmd;
      confirm_cmd.signal_id = 136;
      confirm_cmd.signal_type = "video_file_save_confirmation";
      confirm_cmd.command_id = cmd.command_id;
      confirm_cmd.issued_by = "system";
      confirm_cmd.event_time = confirmed_at;
      confirm_cmd.payload = confirm_payload;

      SignalLogger::log(confirm_cmd);

      std::cout << "[CommandHandlers] Async video save confirmation logged for " << cmd.command_id << "\n";
    }).detach();
  }

  res.data = {{"status", "acknowledged"}, {"message", msg}};
  res.data["command_id"] = cmd.command_id;
  res.data["event_time"] = cmd.event_time;

  return res;
}

CommandResult CommandHandlers::handle_video_capture_pan(Command &cmd) {
  std::cout << "[CommandHandlers] Initiating panoramic video command ("
            << cmd.command_id << ")\n";

  if (privacy_mode_enabled_) {
    CommandResult res;
    res.success = false;
    res.failure_reason = "privacy_mode_active";
    res.data = {
      {"status", "failed"},
      {"error", "privacy_mode_active"},
      {"command_id", cmd.command_id},
      {"event_time", cmd.event_time}
    };
    return res;
  }

  std::string device_id = resolve_device_id(cmd);
  std::optional<std::string> dog_id = resolve_dog_id(cmd);

  int64_t timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count();
  std::string filename = "ORoBase_PAN_VID_" + std::to_string(timestamp) + ".mp4";
  std::string storage_path = "/home/radxa/oro_base_video_audio/" + filename;
  std::string file_id = "PAN_VID_" + cmd.command_id;

  std::thread([device_id, dog_id, cmd, storage_path, file_id]() {
    std::cout << "[CommandHandlers] Starting panoramic sweep capture for " << cmd.command_id << "\n";

    // ── Camera Stepper Architecture Note ──────────────────────────────────
    // The camera rotation stepper motor is wired directly to the Radxa's GPIO
    // pins (gpiochip1 pins 6,7,35 and gpiochip0 pin 38), NOT to the MCU.
    // Commands are sent via ZMQ REQ to ipc:///tmp/oro_mcu_cmd.ipc, where
    // McuSerialReaderNode::command_rep_thread_func intercepts PID_CAMERA_STEPPER
    // packets and routes them to the local stepper_thread_func via
    // set_stepper_target_angle(). The REP returns an immediate ACK, so we must
    // subscribe to the stepper status topic to detect motion completion.
    // ──────────────────────────────────────────────────────────────────────

    // 1. Start video recording

    // Ensure target directory exists
    std::system("mkdir -p /home/radxa/oro_base_video_audio");

    // Terminate any running instances of video_recorder.py
    std::system("pkill -INT -f 'video_recorder.py'");
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // Start video_recorder.py in the background
    std::string record_cmd = "python3 /home/radxa/oro_base/oro_base_input_layer/scripts/video_recorder.py --output " + storage_path + " > /dev/null 2>&1 &";
    std::cout << "[CommandHandlers] Starting video recorder for pan sweep: " << record_cmd << "\n";
    std::system(record_cmd.c_str());

    // Wait for the video recorder to initialize before starting the sweep
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    // 2. Move camera head to limit switch 1 (+90 degrees)
    bool ack_ok = send_stepper_command(9000);
    if (!ack_ok) {
      std::cerr << "[CommandHandlers] Pan failed: Could not send stepper command for +90°\n";
    } else {
      // Wait for limit switch 1 to be pressed
      bool reached = wait_for_limit_switch("/sensors/camera_rotation/limit_switch_1", 25);
      if (!reached) {
        std::cerr << "[CommandHandlers] Pan warning: Limit switch 1 was not pressed within timeout\n";
      }
    }

    // 3. Sweep camera head to limit switch 2 (-90 degrees) while recording
    ack_ok = send_stepper_command(-9000);
    if (!ack_ok) {
      std::cerr << "[CommandHandlers] Pan warning: Could not send stepper command for -90°\n";
    } else {
      // Wait for limit switch 2 to be pressed
      bool reached = wait_for_limit_switch("/sensors/camera_rotation/limit_switch_2", 25);
      if (!reached) {
        std::cerr << "[CommandHandlers] Pan warning: Limit switch 2 was not pressed within timeout\n";
      }
    }

    // 4. Return camera to home position (0 degrees via homing calibration)
    ack_ok = send_stepper_command(99900); // 99900 triggers the homing/calibration sequence
    if (!ack_ok) {
      std::cerr << "[CommandHandlers] Pan warning: Could not send homing command\n";
    } else {
      wait_for_stepper_idle(30);   // homing can take longer due to calibration
    }

    // 5. Stop recording
    std::system("pkill -INT -f 'video_recorder.py'");
    std::this_thread::sleep_for(std::chrono::seconds(2)); // wait for trailer flush

    // 6. Upload video to S3
    std::string upload_cmd = "python3 /home/radxa/oro_base/oro_base_input_layer/scripts/oro_cloud_bridge.py --upload " + storage_path + " --type video > /dev/null 2>&1";
    std::cout << "[CommandHandlers] Uploading panoramic video via Python CLI: " << upload_cmd << "\n";
    int upload_ret = std::system(upload_cmd.c_str());
    bool success = (upload_ret == 0);

    auto now = std::chrono::system_clock::now();
    int64_t confirmed_at = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();

    nlohmann::json confirm_payload = {
        {"file_id", file_id},
        {"storage_path", storage_path},
        {"status", success ? "success" : "failed"}
    };

    Command confirm_cmd;
    confirm_cmd.signal_id = 136;
    confirm_cmd.signal_type = "video_file_save_confirmation";
    confirm_cmd.command_id = cmd.command_id;
    confirm_cmd.issued_by = "system";
    confirm_cmd.event_time = confirmed_at;
    confirm_cmd.payload = confirm_payload;

    SignalLogger::log(confirm_cmd);

    std::cout << "[CommandHandlers] Panoramic video save confirmation logged. Success: " << success << "\n";
  }).detach();

  CommandResult res;
  res.success = true;
  std::string msg = "pan_video_capture_initiated";

  res.data = {{"status", "acknowledged"}, {"message", msg}};
  res.data["command_id"] = cmd.command_id;
  res.data["event_time"] = cmd.event_time;
  res.data["file_id"] = file_id;
  res.data["storage_path"] = storage_path;

  return res;
}

CommandResult CommandHandlers::handle_play_music(Command &cmd) {
  std::cout << "[CommandHandlers] Executing play_music_event ("
            << cmd.command_id << ")\n";

  int action_code = 1; // Default
  if (cmd.payload.contains("action_code")) {
    if (cmd.payload["action_code"].is_number()) {
      action_code = cmd.payload["action_code"].get<int>();
    } else if (cmd.payload["action_code"].is_string()) {
      action_code = std::stoi(cmd.payload["action_code"].get<std::string>());
    }
  } else if (cmd.payload.contains("value")) {
    if (cmd.payload["value"].is_number()) {
      action_code = static_cast<int>(cmd.payload["value"].get<double>());
    } else if (cmd.payload["value"].is_string()) {
      action_code = std::stoi(cmd.payload["value"].get<std::string>());
    }
  }

  // Delegate to RadxaServices
  nlohmann::json radxa_cmd = {
    {"topic", "/commands/audio/speakers"},
    {"value", static_cast<float>(action_code)}
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
  res.data["action_code"] = action_code;
  
  if (cmd.payload.contains("file_id")) {
    res.data["file_id"] = cmd.payload["file_id"];
  }
  if (cmd.payload.contains("storage_path")) {
    res.data["storage_path"] = cmd.payload["storage_path"];
  }

  return res;
}

CommandResult CommandHandlers::handle_stop_music(Command &cmd) {
  std::cout << "[CommandHandlers] Executing stop_music_event ("
            << cmd.command_id << ")\n";

  int action_code = 0; // Stop
  if (cmd.payload.contains("action_code")) {
    if (cmd.payload["action_code"].is_number()) {
      action_code = cmd.payload["action_code"].get<int>();
    } else if (cmd.payload["action_code"].is_string()) {
      action_code = std::stoi(cmd.payload["action_code"].get<std::string>());
    }
  }

  // Delegate to RadxaServices
  nlohmann::json radxa_cmd = {
    {"topic", "/commands/audio/speakers"},
    {"value", static_cast<float>(action_code)}
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
  res.data["action_code"] = action_code;

  if (cmd.payload.contains("file_id")) {
    res.data["file_id"] = cmd.payload["file_id"];
  }
  if (cmd.payload.contains("storage_path")) {
    res.data["storage_path"] = cmd.payload["storage_path"];
  }

  return res;
}

CommandResult CommandHandlers::handle_privacy_mode(Command &cmd) {
  bool enable = false;
  if (cmd.payload.contains("enabled")) {
    if (cmd.payload["enabled"].is_boolean()) {
      enable = cmd.payload["enabled"].get<bool>();
    } else if (cmd.payload["enabled"].is_number()) {
      enable = (cmd.payload["enabled"].get<double>() >= 0.5);
    } else if (cmd.payload["enabled"].is_string()) {
      std::string s = cmd.payload["enabled"].get<std::string>();
      enable = (s == "true" || s == "1");
    }
  }

  privacy_mode_enabled_ = enable;

  std::cout << "[CommandHandlers] Executing privacy_mode ("
            << cmd.command_id << "): " << (enable ? "ENABLE" : "DISABLE") << "\n";

  if (enable) {
    // Gracefully stop active recordings
    std::system("pkill -INT -f 'video_recorder.py'");
    // Send SIGUSR1 (Enable Privacy)
    std::system("pkill -USR1 -f video_ingestor");
    std::system("pkill -USR1 -f audio_ingestor");
  } else {
    // Send SIGUSR2 (Disable Privacy)
    std::system("pkill -USR2 -f video_ingestor");
    std::system("pkill -USR2 -f audio_ingestor");
  }

  CommandResult res;
  res.success = true;
  res.data = {
    {"status", "success"},
    {"privacy_enabled", enable},
    {"command_id", cmd.command_id},
    {"event_time", cmd.event_time}
  };

  return res;
}

} // namespace oro
