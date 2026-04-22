#include "zmq_subscriber.hpp"
#include <iostream>
#include <chrono>

ZmqSubscriber::ZmqSubscriber(HealthMonitor& monitor) : monitor_(monitor) {
    std::cout << "[ZmqSubscriber] Initialized.\n";
    // Setup ZMQ sockets here when real dependency is added
}

ZmqSubscriber::~ZmqSubscriber() {
    stop();
}

void ZmqSubscriber::start() {
    running_ = true;
    std::cout << "[ZmqSubscriber] Listening to topics...\n";
    // In a real implementation:
    // while(running_) {
    //      zmq::message_t topic_msg, payload_msg;
    //      sub_socket_.recv(&topic_msg, ...);
    //      sub_socket_.recv(&payload_msg, ...);
    //      inject_dummy_payload(topic_str, payload_str);
    // }
}

void ZmqSubscriber::stop() {
    running_ = false;
}

uint64_t ZmqSubscriber::get_current_time_ms() {
    auto now = std::chrono::system_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
}

void ZmqSubscriber::inject_dummy_payload(const std::string& topic, const std::string& payload) {
    uint64_t now = get_current_time_ms();
    
    // For now, doing extremely rigid pseudo-parsing logic to dispatch to monitor.
    // Eventually, payload bytes would be deserialized here. Let's assume payload string IS the value for simplicity.

    if (topic == "/system/time/clock") {
        // Just ticking the monitor
        monitor_.tick(now);
    } else if (topic == "/system/device/heartbeat") {
        monitor_.update_device_heartbeat(now);
    } else if (topic == "/system/power/battery_level") {
        try {
            double level = std::stod(payload);
            monitor_.update_battery_level(level, now);
        } catch (...) { std::cerr << "Parse error: " << topic << "\n"; }
    } else if (topic == "/system/camera/frame_quality") {
         try {
            double q = std::stod(payload);
            monitor_.update_frame_quality(q, now);
        } catch (...) { std::cerr << "Parse error: " << topic << "\n"; }
    } else if (topic == "/system/connectivity/state") {
        monitor_.update_device_connectivity_status(payload, now);
    } else if (topic == "/system/power/switch") {
        monitor_.update_power_supply_status(payload, now);
    } else if (topic == "/system/camera/obstruction_status") {
        bool obs = (payload == "true" || payload == "1");
        monitor_.update_camera_obstruction_status(obs, now);
    } else if (topic == "/system/sensor/health") {
        // Assume format "sensor_id:status" for dummy payload
        auto colon = payload.find(':');
        if (colon != std::string::npos) {
            std::string id = payload.substr(0, colon);
            std::string status = payload.substr(colon + 1);
            monitor_.update_sensor_health_status(id, status, now);
        }
    } else if (topic == "/system/sensor/communication") {
        auto colon = payload.find(':');
        if (colon != std::string::npos) {
            std::string id = payload.substr(0, colon);
            std::string status = payload.substr(colon + 1);
            monitor_.update_sensor_communication_status(id, status, now);
        }
    } else {
        std::cerr << "[ZmqSubscriber] Unknown topic: " << topic << "\n";
    }
}
