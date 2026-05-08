#ifndef COMMAND_EXECUTOR_SIGNAL_LOGGER_HPP
#define COMMAND_EXECUTOR_SIGNAL_LOGGER_HPP

#include "command_executor/command.hpp"
#include <unordered_set>
#include <string>
#include <optional>

namespace oro {

class SignalLogger {
public:
  static void log(const Command &cmd);

private:
  // Check if this signal ID should be logged to the events table (UC commands)
  static const std::unordered_set<uint16_t> EVENT_SIGNAL_IDS;
};

} // namespace oro

#endif // COMMAND_EXECUTOR_SIGNAL_LOGGER_HPP
