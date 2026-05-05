#ifndef COMMAND_EXECUTOR_COMMAND_QUEUE_HPP
#define COMMAND_EXECUTOR_COMMAND_QUEUE_HPP

#include "command_executor/command.hpp"
#include <deque>
#include <mutex>
#include <condition_variable>
#include <optional>

namespace oro {

class CommandQueue {
public:
    void push(const Command& cmd) {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push_back(cmd);
        cv_.notify_one();
    }

    std::optional<Command> pop(std::chrono::milliseconds timeout = std::chrono::milliseconds(500)) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (cv_.wait_for(lock, timeout, [this]() { return !queue_.empty(); })) {
            Command cmd = std::move(queue_.front());
            queue_.pop_front();
            return cmd;
        }
        return std::nullopt;
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }

private:
    std::deque<Command> queue_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
};

} // namespace oro

#endif // COMMAND_EXECUTOR_COMMAND_QUEUE_HPP
