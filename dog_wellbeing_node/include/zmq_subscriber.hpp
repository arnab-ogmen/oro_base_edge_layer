#ifndef DW_ZMQ_SUBSCRIBER_HPP
#define DW_ZMQ_SUBSCRIBER_HPP

#include "dog_wellbeing_monitor.hpp"
#include <zmq.hpp>
#include <thread>
#include <atomic>
#include <string>

class ZmqSubscriber {
public:
    ZmqSubscriber(DogWellbeingMonitor& monitor, zmq::context_t& context, const std::string& sensors_endpoint);
    ~ZmqSubscriber();

    void start();
    void stop();

private:
    void thread_func();

    DogWellbeingMonitor& monitor_;
    zmq::context_t& context_;
    std::string sensors_endpoint_;
    
    std::unique_ptr<zmq::socket_t> sub_socket_;
    std::thread thread_;
    std::atomic<bool> running_{false};
};

#endif // DW_ZMQ_SUBSCRIBER_HPP
