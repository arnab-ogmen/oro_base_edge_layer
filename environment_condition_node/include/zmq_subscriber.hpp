#ifndef EC_ZMQ_SUBSCRIBER_HPP
#define EC_ZMQ_SUBSCRIBER_HPP

#include "environment_condition_monitor.hpp"
#include <zmq.hpp>
#include <thread>
#include <atomic>
#include <string>
#include <memory>

class ZmqSubscriber {
public:
    ZmqSubscriber(EnvironmentConditionMonitor& monitor, zmq::context_t& context, const std::string& sensors_endpoint);
    ~ZmqSubscriber();

    void start();
    void stop();

private:
    void thread_func();

    EnvironmentConditionMonitor& monitor_;
    zmq::context_t& context_;
    std::string sensors_endpoint_;
    
    std::unique_ptr<zmq::socket_t> sub_socket_;
    std::thread thread_;
    std::atomic<bool> running_{false};
};

#endif // EC_ZMQ_SUBSCRIBER_HPP
