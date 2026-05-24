#include "zmq_subscriber.hpp"
#include <iostream>
#include <chrono>
#include <cstring>

ZmqSubscriber::ZmqSubscriber(DogWellbeingMonitor& monitor, zmq::context_t& context, const std::string& sensors_endpoint)
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
    sub_socket_->connect(sensors_endpoint_);
    sub_socket_->set(zmq::sockopt::subscribe, "/sensors/food_weight/");
    sub_socket_->set(zmq::sockopt::subscribe, "/sensors/water_level/tank");
    
    std::cout << "[DW ZMQ] Subscribed to " << sensors_endpoint_ 
              << " for /sensors/food_weight/ and /sensors/water_level/tank\n";

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
                    
                    float weight = 0.0f;
                    bool valid = false;
                    if (payload_msg.size() == sizeof(float)) {
                        weight = *static_cast<float*>(payload_msg.data());
                        valid = true;
                    } else if (payload_msg.size() == 14) { // MsgHeader (10 bytes) + float value (4 bytes)
                        std::memcpy(&weight, static_cast<char*>(payload_msg.data()) + 10, sizeof(float));
                        valid = true;
                    }
                    
                    if (valid) {
                        uint64_t current_time = static_cast<uint64_t>(
                            std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::system_clock::now().time_since_epoch()).count());
                        
                        if (topic == "/sensors/food_weight/bowl_1") {
                            monitor_.update_bowl_weight("bowl_1", weight, current_time);
                        } else if (topic == "/sensors/food_weight/bowl_2") {
                            monitor_.update_bowl_weight("bowl_2", weight, current_time);
                        } else if (topic == "/sensors/water_level/tank") {
                            monitor_.update_water_level(static_cast<double>(weight), current_time);
                        }
                    }
                }
            }
        } catch (const zmq::error_t& e) {
            std::cerr << "[DW ZMQ] Error: " << e.what() << "\n";
            break;
        }
    }
    sub_socket_->close();
}
