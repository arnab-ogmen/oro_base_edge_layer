#ifndef COMMAND_EXECUTOR_COMMAND_EXECUTOR_HPP
#define COMMAND_EXECUTOR_COMMAND_EXECUTOR_HPP

#include "command_executor/command.hpp"
#include "command_executor/command_queue.hpp"
#include "command_executor/command_handlers.hpp"
#include <atomic>
#include <thread>
#include <functional>

namespace oro {

class CommandExecutor {
public:
    CommandExecutor(CommandQueue& queue, std::function<void(const Command&)> on_result_cb);
    ~CommandExecutor();

    void start();
    void stop();

private:
    void run();

    CommandQueue& queue_;
    std::function<void(const Command&)> on_result_cb_;
    std::atomic<bool> running_{false};
    std::thread worker_thread_;
};

} // namespace oro

#endif // COMMAND_EXECUTOR_COMMAND_EXECUTOR_HPP
