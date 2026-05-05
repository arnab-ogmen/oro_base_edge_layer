#ifndef COMMAND_EXECUTOR_COMMAND_HPP
#define COMMAND_EXECUTOR_COMMAND_HPP

#include <string>
#include <chrono>
#include <nlohmann/json.hpp>

namespace oro {

enum class CommandStatus : uint8_t {
    RECEIVED,
    QUEUED,
    DISPATCHING,
    COMPLETED,
    FAILED,
    TIMEOUT,
    REJECTED
};

enum class SignalDirection : uint8_t {
    INBOUND,
    OUTBOUND
};

enum class ValueType : uint8_t {
    EVENT,
    ENUM,
    NUMERIC,
    BOOLEAN
};

struct Command {
    uint16_t        signal_id;
    std::string     signal_type;
    std::string     command_id;
    std::string     issued_by;
    int64_t         event_time;
    CommandStatus   status;
    nlohmann::json  payload;
    nlohmann::json  result;

    std::chrono::steady_clock::time_point received_at;
    std::chrono::steady_clock::time_point completed_at;
};

} // namespace oro

#endif // COMMAND_EXECUTOR_COMMAND_HPP
