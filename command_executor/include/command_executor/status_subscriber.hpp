#pragma once

#include "command_executor/command.hpp"
#include "command_executor/command_queue.hpp"
#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <zmq.hpp>

namespace oro {

class StatusSubscriber {
public:
  StatusSubscriber(zmq::context_t &context, CommandQueue &queue);
  ~StatusSubscriber();

  void start();
  void stop();

private:
  void run();

  zmq::context_t &context_;
  CommandQueue &queue_;

  std::unique_ptr<zmq::socket_t> sub_socket_;
  std::atomic<bool> running_{false};
  std::thread worker_thread_;

  // State tracking
  float lid1_state_ = -1.0f; // -1 = unknown, 0 = closed, 1 = open, 3 = transition
  float lid2_state_ = -1.0f;
  float lid1_last_stable_state_ = -1.0f;
  float lid2_last_stable_state_ = -1.0f;
  bool lid1_motor_running_ = false;
  bool lid2_motor_running_ = false;
  bool lid1_system_actuating_ = false;
  bool lid2_system_actuating_ = false;

  void handle_message(const std::string &topic, const zmq::message_t &payload_msg);
  void emit_manual_event(uint8_t lid_id, int signal_id);
};

} // namespace oro
