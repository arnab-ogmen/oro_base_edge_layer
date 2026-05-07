#ifndef HEALTH_ZMQ_SUBSCRIBER_HPP
#define HEALTH_ZMQ_SUBSCRIBER_HPP
// ============================================================================
// zmq_subscriber.hpp — Real ZMQ Subscriber for Health Monitoring
//
// Connects to the Input Layer's three IPC PUB sockets via ZMQ SUB,
// deserializes binary payloads (AnalogPayload / DigitalPayload), and
// dispatches to the HealthMonitor for state evaluation and signal emission.
//
// Threading: runs a single background poller thread that multiplexes
// across all three SUB sockets using zmq::poll().
// ============================================================================

#include "health_monitor.hpp"

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <zmq.hpp>

class ZmqSubscriber {
public:
  /// Construct with references to the HealthMonitor and ZMQ context.
  /// The subscriber will connect to the three IPC endpoints published
  /// by oro_base_input_layer.
  ZmqSubscriber(HealthMonitor &monitor, zmq::context_t &context,
                const std::string &sensor_endpoint,
                const std::string &system_endpoint,
                const std::string &status_endpoint);
  ~ZmqSubscriber();

  // Non-copyable
  ZmqSubscriber(const ZmqSubscriber &) = delete;
  ZmqSubscriber &operator=(const ZmqSubscriber &) = delete;

  /// Launch the background polling thread.
  void start();

  /// Signal the polling thread to stop and join it.
  void stop();

#ifdef ORO_HEALTH_TEST
  /// Debug-only: inject a synthetic payload for testing without ZMQ.
  void inject_dummy_payload(const std::string &topic,
                            const std::string &payload);
#endif

private:
  /// Main poll loop — multiplexes sensor_sub_, system_sub_, status_sub_.
  void run();

  /// Receive and dispatch a multipart message from a single SUB socket.
  /// Returns true if a message was processed.
  bool receive_and_dispatch(zmq::socket_t &socket);

  /// Process a deserialized topic + binary payload pair.
  void dispatch_topic(const std::string &topic, const void *data,
                      size_t data_size);

  /// Get current time in milliseconds since epoch.
  static uint64_t get_current_time_ms();

  HealthMonitor &monitor_;
  zmq::context_t &context_;

  // Three SUB sockets — one per Input Layer PUB socket
  zmq::socket_t sensor_sub_;
  zmq::socket_t system_sub_;
  zmq::socket_t status_sub_;

  std::string sensor_endpoint_;
  std::string system_endpoint_;
  std::string status_endpoint_;

  std::atomic<bool> running_{false};
  std::unique_ptr<std::thread> worker_thread_;
};

#endif // HEALTH_ZMQ_SUBSCRIBER_HPP
