#ifndef COMMAND_EXECUTOR_COMMAND_HANDLERS_HPP
#define COMMAND_EXECUTOR_COMMAND_HANDLERS_HPP

#include "command_executor/command.hpp"
#include <string>
#include <zmq.hpp>
#include <mutex>
#include <optional>

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
  CommandResult handle_camera_rotation(Command &cmd);
  CommandResult handle_video_capture(Command &cmd);
  CommandResult handle_video_capture_pan(Command &cmd);
  CommandResult handle_play_music(Command &cmd);
  CommandResult handle_stop_music(Command &cmd);
  CommandResult handle_privacy_mode(Command &cmd);

private:
  CommandResult send_packet_to_mcu(uint8_t peripheral_id, int32_t value,
                                   int timeout_ms = 6000);

  zmq::context_t context_;
  zmq::socket_t mcu_socket_;
  std::string current_session_id_;
  float current_camera_angle_{0.0f};

  std::mutex continuous_mutex_;
  std::string continuous_storage_path_ = "";
  std::string continuous_file_id_ = "";
  std::string continuous_command_id_ = "";
  std::string continuous_device_id_ = "";
  std::optional<std::string> continuous_dog_id_ = std::nullopt;
  bool privacy_mode_enabled_ = false;
};

} // namespace oro

#endif // COMMAND_EXECUTOR_COMMAND_HANDLERS_HPP
