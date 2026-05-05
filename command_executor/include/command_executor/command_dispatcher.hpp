#ifndef COMMAND_EXECUTOR_COMMAND_DISPATCHER_HPP
#define COMMAND_EXECUTOR_COMMAND_DISPATCHER_HPP

#include "command_executor/command.hpp"
#include <string>
#include <optional>
#include <unordered_set>
#include <mutex>

namespace oro {

class CommandDispatcher {
public:
    std::optional<Command> parse(const std::string& json_str);
    bool validate(Command& cmd);

private:
    std::unordered_set<std::string> active_command_ids_;
    mutable std::mutex mutex_;
};

} // namespace oro

#endif // COMMAND_EXECUTOR_COMMAND_DISPATCHER_HPP
