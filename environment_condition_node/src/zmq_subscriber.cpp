#include "zmq_subscriber.hpp"
#include <iostream>
#include <chrono>
#include <cstring>

ZmqSubscriber::ZmqSubscriber(EnvironmentConditionMonitor& monitor, zmq::context_t& context, const std::string& sensors_endpoint)
    : monitor_(monitor), context_(context), sensors_endpoint_(sensors_endpoint) {}

ZmqSubscriber::~ZmqSubscriber() {
    stop();
}

void ZmqSubscriber::start() {
    if (running_) return;
    running_ = true;
    thread_ = std::thread(&ZmqSubscriber::thread_func, this);
}

void ZmqSubscriber::stop() {
    if (!running_) return;
    running_ = false;
    if (thread_.joinable()) {
        thread_.join();
    }
}

void ZmqSubscriber::thread_func() {
    sub_socket_ = std::make_unique<zmq::socket_t>(context_, zmq::socket_type::sub);
    
    try {
        sub_socket_->connect(sensors_endpoint_);
        sub_socket_->set(zmq::sockopt::subscribe, "/sensors/environment/");
        std::cout << "[EC ZMQ] Subscribed to " << sensors_endpoint_ << " for /sensors/environment/\n";
    } catch (const std::exception& e) {
        std::cerr << "[EC ZMQ] Connect/Subscribe failed: " << e.what() << "\n";
        running_ = false;
        return;
    }

    zmq::pollitem_t items[] = {
        {static_cast<void*>(*sub_socket_), 0, ZMQ_POLLIN, 0}
    };

    while (running_) {
        zmq::message_t topic_msg, payload_msg;
        
        try {
            int rc = zmq::poll(items, 1, std::chrono::milliseconds(100));
            if (rc > 0 && (items[0].revents & ZMQ_POLLIN)) {
                auto res1 = sub_socket_->recv(topic_msg, zmq::recv_flags::none);
                auto res2 = sub_socket_->recv(payload_msg, zmq::recv_flags::none);
                
                if (res1 && res2) {
                    std::string topic(static_cast<char*>(topic_msg.data()), topic_msg.size());
                    
                    float value = 0.0f;
                    uint64_t observed_at = static_cast<uint64_t>(
                        std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch()).count());
                    bool valid = false;
                    
                    if (payload_msg.size() == sizeof(float)) {
                        std::memcpy(&value, payload_msg.data(), sizeof(float));
                        valid = true;
                    } else if (payload_msg.size() == 14) { // MsgHeader (10 bytes) + float value (4 bytes)
                        std::memcpy(&value, static_cast<char*>(payload_msg.data()) + 10, sizeof(float));
                        std::memcpy(&observed_at, static_cast<char*>(payload_msg.data()) + 2, sizeof(uint64_t));
                        valid = true;
                    }
                    
                    if (valid) {
                        if (topic == "/sensors/environment/temperature") {
                            monitor_.update_temperature(static_cast<double>(value), observed_at);
                        } else if (topic == "/sensors/environment/humidity") {
                            monitor_.update_humidity(static_cast<double>(value), observed_at);
                        } else if (topic == "/sensors/environment/light_level" || topic == "/sensors/environment/ambient_light") {
                            monitor_.update_light_level(static_cast<double>(value), observed_at);
                        }
                    }
                }
            }
        } catch (const zmq::error_t& e) {
            std::cerr << "[EC ZMQ] Error: " << e.what() << "\n";
            break;
        }
    }
    
    try {
        sub_socket_->close();
    } catch (...) {}
}
