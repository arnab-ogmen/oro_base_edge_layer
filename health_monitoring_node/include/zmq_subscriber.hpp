#ifndef HEALTH_ZMQ_SUBSCRIBER_HPP
#define HEALTH_ZMQ_SUBSCRIBER_HPP

#include "health_monitor.hpp"
#include <zmq.hpp>
#include <string>
#include <thread>
#include <atomic>
#include <memory>

class ZmqSubscriber {
public:
    ZmqSubscriber(HealthMonitor& monitor, zmq::context_t& context, const std::string& endpoint);
    ~ZmqSubscriber();

    void start();
    void stop();

private:
    void run();
    void process_message(const std::string& topic, const zmq::message_t& payload);

    HealthMonitor& monitor_;
    zmq::context_t& context_;
    std::string endpoint_;

    zmq::socket_t sub_socket_;
    std::atomic<bool> running_{false};
    std::unique_ptr<std::thread> worker_thread_;
};

#endif // HEALTH_ZMQ_SUBSCRIBER_HPP
