// ============================================================================
// zmq_subscriber.cpp — Real ZMQ Subscriber Implementation
//
// Connects to Input Layer IPC endpoints, deserializes binary ORo payloads
// (AnalogPayload / DigitalPayload), and dispatches to HealthMonitor.
//
// Wire format (from McuSerialReaderNode):
//   Frame 0: Topic string (e.g., "/system/device/heartbeat")
//   Frame 1: Binary payload (AnalogPayload=14B, DigitalPayload=11B)
//
// The subscriber uses zmq::poll() to multiplex across three SUB sockets
// with a 100ms timeout, allowing graceful shutdown.
// ============================================================================

#include "zmq_subscriber.hpp"
#include "config.hpp"

#include <chrono>
#include <cstring>
#include <iostream>

// ── Binary Payload Structs (matching sensor_payloads.hpp in input_layer) ────
// These are duplicated here to avoid a cross-package include dependency.
// They MUST remain in sync with
// oro_base_input_layer/include/data/sensor_payloads.hpp.

namespace wire {

struct MsgHeader {
  uint8_t sensor_id;
  uint8_t seq_num;
  uint64_t timestamp_ms;
} __attribute__((packed));

static_assert(sizeof(MsgHeader) == 10, "MsgHeader must be exactly 10 bytes");

struct AnalogPayload {
  MsgHeader header;
  float value;
} __attribute__((packed));

static_assert(sizeof(AnalogPayload) == 14,
              "AnalogPayload must be exactly 14 bytes");

struct DigitalPayload {
  MsgHeader header;
  uint8_t state;
} __attribute__((packed));

static_assert(sizeof(DigitalPayload) == 11,
              "DigitalPayload must be exactly 11 bytes");

} // namespace wire

// ── Constructor / Destructor ────────────────────────────────────────────────

ZmqSubscriber::ZmqSubscriber(HealthMonitor &monitor, zmq::context_t &context,
                             const std::string &sensor_endpoint,
                             const std::string &system_endpoint,
                             const std::string &status_endpoint)
    : monitor_(monitor), context_(context),
      sensor_sub_(context, zmq::socket_type::sub),
      system_sub_(context, zmq::socket_type::sub),
      status_sub_(context, zmq::socket_type::sub),
      sensor_endpoint_(sensor_endpoint), system_endpoint_(system_endpoint),
      status_endpoint_(status_endpoint) {

  // ── Connect to Input Layer PUB sockets ──────────────────────────────────

  sensor_sub_.connect(sensor_endpoint_);
  system_sub_.connect(system_endpoint_);
  status_sub_.connect(status_endpoint_);

  // ── Subscribe to relevant topic prefixes ────────────────────────────────

  // From system_pub_: heartbeat, battery, power, connectivity
  system_sub_.set(zmq::sockopt::subscribe, "/system/device/heartbeat");
  system_sub_.set(zmq::sockopt::subscribe, "/system/power/battery_level");
  system_sub_.set(zmq::sockopt::subscribe, "/system/power/switch");
  system_sub_.set(zmq::sockopt::subscribe, "/system/connectivity/state");

  // From status_pub_: water pump (for fountain signals)
  status_sub_.set(zmq::sockopt::subscribe, "/status/water_pump");

  // From sensor_pub_: camera frame quality, obstruction (future)
  sensor_sub_.set(zmq::sockopt::subscribe, "/sensors/camera");

  std::cout << "[ZmqSubscriber] Connected to:\n"
            << "  SENSOR: " << sensor_endpoint_ << "\n"
            << "  SYSTEM: " << system_endpoint_ << "\n"
            << "  STATUS: " << status_endpoint_ << "\n";
}

ZmqSubscriber::~ZmqSubscriber() { stop(); }

// ── Start / Stop ────────────────────────────────────────────────────────────

void ZmqSubscriber::start() {
  if (running_.load())
    return;

  running_ = true;
  worker_thread_ = std::make_unique<std::thread>(&ZmqSubscriber::run, this);
  std::cout << "[ZmqSubscriber] Polling thread started.\n";
}

void ZmqSubscriber::stop() {
  if (!running_.load())
    return;

  running_ = false;
  if (worker_thread_ && worker_thread_->joinable()) {
    worker_thread_->join();
  }
  std::cout << "[ZmqSubscriber] Polling thread stopped.\n";
}

// ── Main Poll Loop ──────────────────────────────────────────────────────────

void ZmqSubscriber::run() {
  std::cout << "[ZmqSubscriber] Entering poll loop...\n";

  // Set receive timeout on all sockets (for graceful shutdown)
  int rcv_timeout_ms = 100;
  sensor_sub_.set(zmq::sockopt::rcvtimeo, rcv_timeout_ms);
  system_sub_.set(zmq::sockopt::rcvtimeo, rcv_timeout_ms);
  status_sub_.set(zmq::sockopt::rcvtimeo, rcv_timeout_ms);

  // Build poll items
  std::array<zmq::pollitem_t, 3> poll_items = {{
      {static_cast<void *>(sensor_sub_), 0, ZMQ_POLLIN, 0},
      {static_cast<void *>(system_sub_), 0, ZMQ_POLLIN, 0},
      {static_cast<void *>(status_sub_), 0, ZMQ_POLLIN, 0},
  }};

  while (running_.load(std::memory_order_relaxed)) {
    // Poll all three sockets with 100ms timeout
    int rc = zmq::poll(poll_items.data(), static_cast<int>(poll_items.size()),
                       std::chrono::milliseconds(100));

    if (rc <= 0) {
      // Timeout or error — loop back and check running_
      continue;
    }

    // Check each socket for available messages
    if (poll_items[0].revents & ZMQ_POLLIN) {
      receive_and_dispatch(sensor_sub_);
    }
    if (poll_items[1].revents & ZMQ_POLLIN) {
      receive_and_dispatch(system_sub_);
    }
    if (poll_items[2].revents & ZMQ_POLLIN) {
      receive_and_dispatch(status_sub_);
    }
  }

  std::cout << "[ZmqSubscriber] Exiting poll loop.\n";
}

// ── Receive & Dispatch ──────────────────────────────────────────────────────

bool ZmqSubscriber::receive_and_dispatch(zmq::socket_t &socket) {
  try {
    // Frame 0: Topic string
    zmq::message_t topic_msg;
    auto res = socket.recv(topic_msg, zmq::recv_flags::dontwait);
    if (!res) {
      return false;
    }

    std::string topic(static_cast<char *>(topic_msg.data()), topic_msg.size());

    // Check if there is a second frame (binary payload)
    bool more = socket.get(zmq::sockopt::rcvmore);

    if (more) {
      zmq::message_t payload_msg;
      auto payload_res = socket.recv(payload_msg, zmq::recv_flags::dontwait);
      if (payload_res) {
        dispatch_topic(topic, payload_msg.data(), payload_msg.size());
      }
    }

    return true;
  } catch (const zmq::error_t &e) {
    if (running_.load()) {
      std::cerr << "[ZmqSubscriber] ZMQ error: " << e.what() << "\n";
    }
    return false;
  }
}

// ── Topic Dispatch ──────────────────────────────────────────────────────────

void ZmqSubscriber::dispatch_topic(const std::string &topic, const void *data,
                                   size_t data_size) {
  uint64_t now = get_current_time_ms();

  // ── /system/device/heartbeat — DigitalPayload ─────────────────────────
  if (topic == "/system/device/heartbeat") {
    if (data_size >= sizeof(wire::DigitalPayload)) {
      monitor_.update_device_heartbeat(now);
    }
    return;
  }

  // ── /system/power/battery_level — AnalogPayload ───────────────────────
  if (topic == "/system/power/battery_level") {
    if (data_size >= sizeof(wire::AnalogPayload)) {
      wire::AnalogPayload payload;
      std::memcpy(&payload, data, sizeof(payload));
      monitor_.update_battery_level(static_cast<double>(payload.value),
                                    "default", "unknown", now);
    }
    return;
  }

  // ── /system/power/switch — DigitalPayload ─────────────────────────────
  if (topic == "/system/power/switch") {
    if (data_size >= sizeof(wire::DigitalPayload)) {
      wire::DigitalPayload payload;
      std::memcpy(&payload, data, sizeof(payload));
      // Map state byte to categorical text
      std::string status;
      switch (payload.state) {
      case 0:
        status = "off";
        break;
      case 1:
        status = "on_mains";
        break;
      case 2:
        status = "on_battery";
        break;
      default:
        status = "unknown";
        break;
      }
      monitor_.update_power_supply_status(status, "mains", "normal", now);
    }
    return;
  }

  // ── /system/connectivity/state — DigitalPayload ───────────────────────
  if (topic == "/system/connectivity/state") {
    if (data_size >= sizeof(wire::DigitalPayload)) {
      wire::DigitalPayload payload;
      std::memcpy(&payload, data, sizeof(payload));
      // Map state byte: 0=disconnected, 1=local_only, 2=connected
      std::string status;
      std::string signal_strength = "";
      switch (payload.state) {
      case 0:
        status = "disconnected";
        break;
      case 1:
        status = "local_only";
        break;
      case 2:
        status = "connected";
        break;
      default:
        status = "unknown";
        break;
      }
      monitor_.update_device_connectivity_status(status, "wifi",
                                                 signal_strength, now);
    }
    return;
  }

  // ── /status/water_pump — DigitalPayload ───────────────────────────────
  // Derives both #127 fountain_pump_health_status and #128
  // water_fountain_status
  if (topic == "/status/water_pump") {
    if (data_size >= sizeof(wire::DigitalPayload)) {
      wire::DigitalPayload payload;
      std::memcpy(&payload, data, sizeof(payload));

      // Pump state: 0=off, 1=on
      std::string pump_status = (payload.state == 1) ? "healthy" : "off";
      std::string fountain_status = (payload.state == 1) ? "active" : "idle";

      monitor_.update_fountain_pump_health("pump_0", pump_status, "", now);
      monitor_.update_water_fountain_status(
          "fountain_0", fountain_status,
          (payload.state == 1) ? "normal" : "standby", now);
    }
    return;
  }

  // ── /sensors/camera/* — Camera-related signals ────────────────────────
  // Camera obstruction and frame quality would be published here by
  // a vision subsystem. For now, these topics exist as subscription targets.
  // Future: /sensors/camera/obstruction → update_camera_obstruction_status
  //         /sensors/camera/frame_quality → update_frame_quality

  // Unknown topic — log for debugging during development
  // std::cerr << "[ZmqSubscriber] Unhandled topic: " << topic
  //           << " (" << data_size << " bytes)\n";
}

// ── Time Helper ─────────────────────────────────────────────────────────────

uint64_t ZmqSubscriber::get_current_time_ms() {
  auto now = std::chrono::system_clock::now();
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          now.time_since_epoch())
          .count());
}

// ── Debug / Test Support ────────────────────────────────────────────────────

#ifdef ORO_HEALTH_TEST

void ZmqSubscriber::inject_dummy_payload(const std::string &topic,
                                         const std::string &payload) {
  uint64_t now = get_current_time_ms();

  if (topic == "/system/device/heartbeat") {
    monitor_.update_device_heartbeat(now);
  } else if (topic == "/system/power/battery_level") {
    try {
      double level = std::stod(payload);
      monitor_.update_battery_level(level, "default", "unknown", now);
    } catch (...) {
      std::cerr << "Parse error: " << topic << "\n";
    }
  } else if (topic == "/system/connectivity/state") {
    monitor_.update_device_connectivity_status(payload, "wifi", "", now);
  } else if (topic == "/system/power/switch") {
    monitor_.update_power_supply_status(payload, "mains", "normal", now);
  } else if (topic == "/system/camera/obstruction_status") {
    bool obs = (payload == "true" || payload == "1");
    monitor_.update_camera_obstruction_status(obs, "cam_0", 1.0, now);
  } else if (topic == "/system/camera/frame_quality") {
    try {
      double q = std::stod(payload);
      monitor_.update_frame_quality(q, "cam_0", "", now);
    } catch (...) {
      std::cerr << "Parse error: " << topic << "\n";
    }
  } else if (topic == "/system/sensor/health") {
    auto colon = payload.find(':');
    if (colon != std::string::npos) {
      std::string id = payload.substr(0, colon);
      std::string status = payload.substr(colon + 1);
      monitor_.update_sensor_health_status(id, status, "", now);
    }
  } else if (topic == "/system/sensor/communication") {
    auto colon = payload.find(':');
    if (colon != std::string::npos) {
      std::string id = payload.substr(0, colon);
      std::string status = payload.substr(colon + 1);
      monitor_.update_sensor_communication_status(id, status, "", now);
    }
  } else if (topic == "/status/water_pump") {
    // 0=off, 1=on
    uint8_t state = (payload == "1" || payload == "on") ? 1 : 0;
    std::string pump_status = (state == 1) ? "healthy" : "off";
    std::string fountain_status = (state == 1) ? "active" : "idle";
    monitor_.update_fountain_pump_health("pump_0", pump_status, "", now);
    monitor_.update_water_fountain_status("fountain_0", fountain_status,
                                          (state == 1) ? "normal" : "standby",
                                          now);
  } else {
    std::cerr << "[ZmqSubscriber] Unknown test topic: " << topic << "\n";
  }
}

#endif // ORO_HEALTH_TEST
