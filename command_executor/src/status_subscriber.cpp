#include "command_executor/status_subscriber.hpp"
#include <chrono>
#include <iostream>
#include <nlohmann/json.hpp>

namespace oro {

StatusSubscriber::StatusSubscriber(zmq::context_t &context, CommandQueue &queue)
    : context_(context), queue_(queue) {
  sub_socket_ =
      std::make_unique<zmq::socket_t>(context_, zmq::socket_type::sub);
  sub_socket_->connect("ipc:///tmp/oro_status.ipc");

  // Subscribe to relevant topics
  sub_socket_->set(zmq::sockopt::subscribe, "/status/lid/1");
  sub_socket_->set(zmq::sockopt::subscribe, "/status/lid/2");
  sub_socket_->set(zmq::sockopt::subscribe, "/status/lid_motor/1");
  sub_socket_->set(zmq::sockopt::subscribe, "/status/lid_motor/2");
}

StatusSubscriber::~StatusSubscriber() { stop(); }

void StatusSubscriber::start() {
  running_ = true;
  worker_thread_ = std::thread(&StatusSubscriber::run, this);
}

void StatusSubscriber::stop() {
  if (running_) {
    running_ = false;
    if (worker_thread_.joinable()) {
      worker_thread_.join();
    }
  }
}

void StatusSubscriber::run() {
  std::cout << "[StatusSubscriber] Worker thread started.\n";
  sub_socket_->set(zmq::sockopt::rcvtimeo, 500);

  while (running_) {
    zmq::message_t topic_msg;
    auto res = sub_socket_->recv(topic_msg, zmq::recv_flags::none);
    if (!res) {
      continue;
    }

    if (!topic_msg.more()) {
      continue; // Expected multipart message
    }

    zmq::message_t payload_msg;
    auto res_payload = sub_socket_->recv(payload_msg, zmq::recv_flags::none);
    if (!res_payload) {
      continue;
    }

    std::string topic(static_cast<char *>(topic_msg.data()), topic_msg.size());
    handle_message(topic, payload_msg);
  }
  std::cout << "[StatusSubscriber] Worker thread stopped.\n";
}

void StatusSubscriber::handle_message(const std::string &topic,
                                      const zmq::message_t &payload_msg) {
  // Use a simplified parsing logic by casting the struct directly.
  // Note: We avoid including the entire sensor_payloads.hpp here to keep
  // dependencies low, we just need the float value or uint8_t state.

  if (topic == "/status/lid_motor/1" || topic == "/status/lid_motor/2") {
    if (payload_msg.size() != 11)
      return; // sizeof(DigitalPayload)
    const uint8_t *data = static_cast<const uint8_t *>(payload_msg.data());
    uint8_t state = data[10]; // MsgHeader is 10 bytes, state is byte 10

    if (topic == "/status/lid_motor/1") {
      lid1_motor_running_ = (state != 0);
      if (lid1_motor_running_) {
        lid1_manual_transition_ = false;
        lid1_system_actuating_ = true;
      }
    } else {
      lid2_motor_running_ = (state != 0);
      if (lid2_motor_running_) {
        lid2_manual_transition_ = false;
        lid2_system_actuating_ = true;
      }
    }
  } else if (topic == "/status/lid/1" || topic == "/status/lid/2") {
    if (payload_msg.size() != 14)
      return; // sizeof(AnalogPayload)
    const uint8_t *data = static_cast<const uint8_t *>(payload_msg.data());
    float value;
    std::memcpy(&value, data + 10, sizeof(float)); // MsgHeader is 10 bytes

    if (topic == "/status/lid/1") {
      if (value == 3.0f) {
        // Transition state: arm the manual trigger if motor isn't running
        // if (!lid1_motor_running_) {
        lid1_manual_transition_ = true;
        // }
      }
      // else if (value == 1.0f) {
      if (value == 1.0f) {
        // Just reached OPEN. Check if we came from CLOSED via a manual
        // transition.
        if (lid1_manual_transition_ && lid1_last_stable_state_ == 0.0f) {
          std::cout << "[StatusSubscriber] Manual move: CLOSED -> TRANSITION "
                       "-> OPEN on Lid 1\n";
          emit_manual_event(1, 84);
        }
        lid1_last_stable_state_ = 1.0f;
        lid1_manual_transition_ = false;
      } else if (value == 0.0f) {
        // Just reached CLOSED. Check if we came from OPEN via a manual
        // transition.
        if (lid1_manual_transition_ && lid1_last_stable_state_ == 1.0f) {
          std::cout << "[StatusSubscriber] Manual move: OPEN -> TRANSITION -> "
                       "CLOSED on Lid 1\n";
          emit_manual_event(1, 123);
        }
        lid1_last_stable_state_ = 0.0f;
        lid1_manual_transition_ = false;
      }
      lid1_state_ = value;
    } else if (topic == "/status/lid/2") {
      if (value == 3.0f) {
        // if (!lid2_motor_running_) {
        lid2_manual_transition_ = true;
        // }
      }

      // else if (value == 1.0f) {

      if (value == 1.0f) {
        if (lid2_manual_transition_ && lid2_last_stable_state_ == 0.0f) {
          std::cout << "[StatusSubscriber] Manual move: CLOSED -> TRANSITION "
                       "-> OPEN on Lid 2\n";
          emit_manual_event(2, 84);
        }
        lid2_last_stable_state_ = 1.0f;
        lid2_manual_transition_ = false;
      } else if (value == 0.0f) {
        if (lid2_manual_transition_ && lid2_last_stable_state_ == 1.0f) {
          std::cout << "[StatusSubscriber] Manual move: OPEN -> TRANSITION -> "
                       "CLOSED on Lid 2\n";
          emit_manual_event(2, 123);
        }
        lid2_last_stable_state_ = 0.0f;
        lid2_manual_transition_ = false;
      }
      lid2_state_ = value;
    }
  }
}

void StatusSubscriber::emit_manual_event(uint8_t lid_id, int signal_id) {
  auto now_steady = std::chrono::steady_clock::now();
  auto &last_time =
      (lid_id == 1) ? lid1_last_event_time_ : lid2_last_event_time_;

  auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        now_steady - last_time)
                        .count();

  // 1000ms debounce to prevent rapid flapping/noise logs
  if (last_time.time_since_epoch().count() > 0 && elapsed_ms < 1000) {
    return;
  }
  last_time = now_steady;

  Command cmd;
  cmd.signal_id = signal_id;
  cmd.signal_type = (signal_id == 84) ? "manual_lid_open_command_event"
                                      : "manual_lid_close_command_event";

  // Generate a pseudo command ID since it originated from hardware
  auto now = std::chrono::system_clock::now();
  auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                       now.time_since_epoch())
                       .count();
  cmd.command_id =
      "hw_evt_" + std::to_string(signal_id) + "_" + std::to_string(timestamp);
  cmd.issued_by = "status_subscriber";
  cmd.event_time = static_cast<int64_t>(timestamp);
  cmd.status = CommandStatus::RECEIVED;
  cmd.received_at = std::chrono::steady_clock::now();

  // Create payload to match expected structure
  nlohmann::json payload;
  payload["lid_id"] = lid_id;

  // For #84, action could be 1, for #123 action could be 0, if required by
  // other parts
  payload["action"] = (signal_id == 84) ? 1 : 0;

  cmd.payload = payload;

  std::cout << "[StatusSubscriber] Emitted Command: { "
            << "command_id: \"" << cmd.command_id << "\", "
            << "signal_id: " << cmd.signal_id << ", "
            << "signal_type: \"" << cmd.signal_type << "\", "
            << "payload: " << cmd.payload.dump() << " }\n";

  queue_.push(cmd);
}

} // namespace oro
