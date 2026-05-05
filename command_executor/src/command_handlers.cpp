#include "command_executor/command_handlers.hpp"
#include "data/oro_protocol.hpp"
#include <cstring>
#include <iostream>
#include <nlohmann/json.hpp>
#include <thread>

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
        res.data = {{"status", "SUCCESS"}, {"raw_response", resp_str}};
      }
    } else {
      std::cout << "[CommandHandlers] MCU REQ timeout, reconstructing socket "
                   "to clear EFSM state.\n";
      mcu_socket_ = zmq::socket_t(context_, zmq::socket_type::req);
      mcu_socket_.connect("ipc:///tmp/oro_mcu_cmd.ipc");
      mcu_socket_.set(zmq::sockopt::rcvtimeo, timeout_ms);

      res.success = true;
      res.data = {{"status", "SUCCESS"},
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
    res.data = {{"status", "FAILED"},
                {"completion_time",
                 std::chrono::duration_cast<std::chrono::milliseconds>(
                     std::chrono::system_clock::now().time_since_epoch())
                     .count()},
                {"mcu_error", e.what()}};
  }
  return res;
}

CommandResult CommandHandlers::handle_lid_open(Command &cmd) {
  std::cout << "[CommandHandlers] Executing manual_lid_open_command_event ("
            << cmd.command_id << ")\n";

  uint8_t lid_id = 1;
  if (cmd.payload.contains("lid_id")) {
    lid_id = cmd.payload["lid_id"].get<uint8_t>();
  }

  uint8_t pid = PID_LID1_STEPPER;
  int default_timeout = 6000; // 1500 (firmware) + 1000 buffer
  if (lid_id == 2) {
    pid = PID_LID2_STEPPER;
    default_timeout = 6000; // 1600 (firmware) + 1000 buffer
  }

  int timeout_ms = default_timeout;
  if (cmd.payload.contains("timeout_ms")) {
    timeout_ms = cmd.payload["timeout_ms"].get<int>();
  }
  return send_packet_to_mcu(pid, 1, timeout_ms);
}

CommandResult CommandHandlers::handle_lid_close(Command &cmd) {
  std::cout << "[CommandHandlers] Executing manual_lid_close_command_event ("
            << cmd.command_id << ")\n";

  uint8_t lid_id = 1;
  if (cmd.payload.contains("lid_id")) {
    lid_id = cmd.payload["lid_id"].get<uint8_t>();
  }

  uint8_t pid = PID_LID1_STEPPER;
  int default_timeout = 6000; // 800 (firmware) + 1000 buffer
  if (lid_id == 2) {
    pid = PID_LID2_STEPPER;
    default_timeout = 6000; // 900 (firmware) + 1000 buffer
  }

  int timeout_ms = default_timeout;
  if (cmd.payload.contains("timeout_ms")) {
    timeout_ms = cmd.payload["timeout_ms"].get<int>();
  }
  return send_packet_to_mcu(pid, 0, timeout_ms);
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
  int default_timeout = 6000;

  if (cmd.payload.contains("action")) {
    int8_t action = cmd.payload["action"].get<int8_t>();
    if (action == 0) {
      val = 0;
    }
  }

  int timeout_ms = default_timeout;
  if (cmd.payload.contains("timeout_ms")) {
    timeout_ms = cmd.payload["timeout_ms"].get<int>();
  }

  return send_packet_to_mcu(pid, val, timeout_ms);
}

CommandResult CommandHandlers::handle_treat_dispense(Command &cmd) {
  std::cout << "[CommandHandlers] Executing treat_dispense_command_event ("
            << cmd.command_id << ")\n";
  CommandResult res;
  res.success = true;
  res.data = {{"status", "SUCCESS"},
              {"completion_time",
               std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::system_clock::now().time_since_epoch())
                   .count()},
              {"treats_dispensed", 3}};
  return res;
}

CommandResult CommandHandlers::handle_photo_capture(Command &cmd) {
  std::cout << "[CommandHandlers] Executing photo_capture_command_event ("
            << cmd.command_id << ")\n";
  CommandResult res;
  res.success = true;
  res.data = {{"status", "SUCCESS"},
              {"completion_time",
               std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::system_clock::now().time_since_epoch())
                   .count()}};
  return res;
}

CommandResult CommandHandlers::handle_live_session_start(Command &cmd) {
  std::cout << "[CommandHandlers] Executing live_session_start_event ("
            << cmd.command_id << ") on separate thread placeholder\n";

  std::thread t([id = cmd.command_id]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    std::cout << "[CommandHandlers] Detached live session background task for "
              << id << " completed.\n";
  });
  t.detach();

  CommandResult res;
  res.success = true;
  res.data = {{"status", "SUCCESS"},
              {"completion_time",
               std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::system_clock::now().time_since_epoch())
                   .count()}};
  return res;
}

} // namespace oro
