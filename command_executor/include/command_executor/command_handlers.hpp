#ifndef COMMAND_EXECUTOR_COMMAND_HANDLERS_HPP
#define COMMAND_EXECUTOR_COMMAND_HANDLERS_HPP

#include "command_executor/command.hpp"
#include <string>
#include <zmq.hpp>

namespace oro {

struct CommandResult {
  bool success;
  std::string failure_reason;
  nlohmann::json data;
};

class CommandHandlers {
public:
  CommandHandlers();
  ~CommandHandlers() = default;

  CommandResult handle_manual_lid_open(Command &cmd);
  CommandResult handle_manual_lid_close(Command &cmd);
  CommandResult handle_lid_actuation(Command &cmd);
  CommandResult handle_treat_dispense(Command &cmd);
  CommandResult handle_photo_capture(Command &cmd);
  CommandResult handle_live_session_start(Command &cmd);
  CommandResult handle_live_session_end(Command &cmd);
  CommandResult handle_settings_apply(Command &cmd);

private:
  CommandResult send_packet_to_mcu(uint8_t peripheral_id, int32_t value,
                                   int timeout_ms = 6000);

  zmq::context_t context_;
  zmq::socket_t mcu_socket_;
  std::string current_session_id_;
};

} // namespace oro

#endif // COMMAND_EXECUTOR_COMMAND_HANDLERS_HPP
