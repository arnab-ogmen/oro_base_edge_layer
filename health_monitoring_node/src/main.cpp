#include "health_monitor.hpp"
#include "storage_handoff/storage_handoff.hpp"
#include "zmq_subscriber.hpp"

#include <chrono>
#include <iostream>
#include <thread>

int main() {
  std::cout << "Starting Health Monitoring Node...\n";

  // 1. Initialize Storage Handoff Library
  storage_handoff::StorageWriter storage_writer;

  // 2. Initialize Health Monitor (State Cache & Rule Engine)
  HealthMonitor monitor(storage_writer);

  // 3. Initialize & Start ZMQ Subscriber
  ZmqSubscriber subscriber(monitor);

  // --- MOCK TESTING ---
  std::cout << "\n[TEST] Emulating initial payloads...\n";

  // Config change
  monitor.update_timeout_config(15.0, 1000000);
  monitor.update_battery_low_config(20.0, 1000001);

  // Initial signals (should trigger change-based writes)
  subscriber.inject_dummy_payload("/system/connectivity/state", "connected");
  subscriber.inject_dummy_payload("/system/power/switch", "on_mains");

  // Heartbeat logic
  subscriber.inject_dummy_payload("/system/device/heartbeat", "ping");

  // Battery at 80% (initial trigger)
  subscriber.inject_dummy_payload("/system/power/battery_level", "80.0");

  std::cout
      << "\n[TEST] Emulating identical payloads (Should not log to DB)...\n";
  subscriber.inject_dummy_payload("/system/connectivity/state", "connected");
  subscriber.inject_dummy_payload("/system/power/battery_level",
                                  "80.5"); // Only 0.5% shift, shouldn't trigger
  subscriber.inject_dummy_payload(
      "/system/device/heartbeat",
      "ping"); // Happens instantly, so time delta is small, shouldn't trigger

  std::cout << "\n[TEST] Emulating threshold breaking payloads...\n";
  subscriber.inject_dummy_payload("/system/connectivity/state", "disconnected");
  subscriber.inject_dummy_payload("/system/power/battery_level",
                                  "78.0"); // 2.0% shift, should trigger

  subscriber.start(); // In real life, blocks or runs on thread

  return 0;
}
