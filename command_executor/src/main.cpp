#include "command_executor/command.hpp"
#include "command_executor/command_queue.hpp"
#include "command_executor/command_dispatcher.hpp"
#include "command_executor/command_executor.hpp"
#include "command_executor/status_subscriber.hpp"
#include <iostream>
#include <csignal>
#include <atomic>
#include <zmq.hpp>

std::atomic<bool> g_running{true};

void signal_handler(int) {
    std::cout << "\n[CommandExecutor] Terminating service...\n";
    g_running = false;
}

int main() {
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    std::cout << "[CommandExecutor] Initializing standalone service...\n";

    zmq::context_t context(1);

    // ZMQ PULL socket to receive inbound commands from CommandIngressNode
    zmq::socket_t pull_socket(context, zmq::socket_type::pull);
    pull_socket.bind("ipc:///tmp/oro_cmd_exec.ipc");
    std::cout << "[CommandExecutor] Bound to ipc:///tmp/oro_cmd_exec.ipc (PULL)\n";

    // ZMQ PUSH socket to send results back to CommandIngressNode
    zmq::socket_t push_socket(context, zmq::socket_type::push);
    push_socket.bind("ipc:///tmp/oro_cmd_result.ipc");
    std::cout << "[CommandExecutor] Bound to ipc:///tmp/oro_cmd_result.ipc (PUSH)\n";

    oro::CommandQueue queue;
    oro::CommandDispatcher dispatcher;

    // Response callback to serialize and push results back to CommandIngressNode
    auto on_result_cb = [&push_socket](const oro::Command& cmd) {
        try {
            nlohmann::json res_json;
            res_json["header"] = {
                {"signal_id", cmd.signal_id},
                {"signal_type", cmd.signal_type},
                {"command_id", cmd.command_id},
                {"source", "UCES"}
            };
            res_json["result"] = cmd.result;

            std::string res_str = res_json.dump();
            std::cout << "[CommandExecutor] Emitting result JSON for command " << cmd.command_id << "\n";
            zmq::message_t msg(res_str.data(), res_str.size());
            push_socket.send(msg, zmq::send_flags::none);
        } catch (const std::exception& e) {
            std::cerr << "[CommandExecutor] Error in result callback: " << e.what() << "\n";
        }
    };

    oro::StatusSubscriber subscriber(context, queue);
    subscriber.start();

    oro::CommandExecutor executor(queue, on_result_cb);
    executor.start();

    std::cout << "[CommandExecutor] Processing incoming commands loop...\n";
    while (g_running) {
        zmq::message_t msg;
        // non-blocking pull to allow signal handler termination
        auto recv_res = pull_socket.recv(msg, zmq::recv_flags::dontwait);
        if (!recv_res) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        std::string cmd_str(static_cast<char*>(msg.data()), msg.size());
        std::cout << "[CommandExecutor] Received JSON payload from input layer: " << cmd_str << "\n";

        auto cmd_opt = dispatcher.parse(cmd_str);
        if (!cmd_opt) {
            std::cerr << "[CommandExecutor] Command parsing failed.\n";
            continue;
        }

        auto& cmd = *cmd_opt;
        if (!dispatcher.validate(cmd)) {
            std::cerr << "[CommandExecutor] Command validation failed for " << cmd.command_id << ".\n";
            // Return validation failure immediately
            oro::Command failed_cmd = cmd;
            failed_cmd.status = oro::CommandStatus::REJECTED;
            failed_cmd.result = {{"status", "REJECTED"}, {"reason", "Validation failed"}};
            on_result_cb(failed_cmd);
            continue;
        }

        // Successfully validated, push to queue
        queue.push(cmd);
        std::cout << "[CommandExecutor] Successfully queued " << cmd.command_id << ". Current queue size: " << queue.size() << "\n";
    }

    executor.stop();
    subscriber.stop();
    std::cout << "[CommandExecutor] Service shutdown complete.\n";
    return 0;
}
