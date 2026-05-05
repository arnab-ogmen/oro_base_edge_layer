#ifndef COMMAND_EXECUTOR_SIGNAL_LOGGER_HPP
#define COMMAND_EXECUTOR_SIGNAL_LOGGER_HPP

#include "command_executor/command.hpp"

namespace oro {

class SignalLogger {
public:
    static void log(const Command& cmd);
};

} // namespace oro

#endif // COMMAND_EXECUTOR_SIGNAL_LOGGER_HPP
