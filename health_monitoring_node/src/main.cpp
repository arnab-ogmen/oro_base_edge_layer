// ============================================================================
// main.cpp — Health Monitoring Node Entry Point
//
// Production event loop for the Health Monitoring Node:
//   1. Initializes StorageWriter (mock → future real DB)
//   2. Creates HealthMonitor with state cache and rule engine
//   3. Launches ZmqSubscriber with three real SUB sockets
//   4. Runs main tick loop for periodic signal emissions
//   5. Handles SIGINT/SIGTERM for graceful shutdown
// ============================================================================

#include "config.hpp"
#include "health_monitor.hpp"
#include "storage_handoff/storage_handoff.hpp"
#include "zmq_subscriber.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <thread>

// ── Globals ─────────────────────────────────────────────────────────────────

static std::atomic<bool> g_running{true};

static void signal_handler(int /*sig*/) {
  std::cout << "\n[HealthMonitoringNode] Shutdown signal received.\n";
  g_running = false;
}

// ── Time Helper ─────────────────────────────────────────────────────────────

static uint64_t now_ms() {
  auto now = std::chrono::system_clock::now();
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          now.time_since_epoch())
          .count());
}

// ── Main ────────────────────────────────────────────────────────────────────

int main() {
  std::signal(SIGINT, signal_handler);
  std::signal(SIGTERM, signal_handler);

  std::cout << "═══════════════════════════════════════════════════════\n"
            << "  ORo Base — Health Monitoring Node\n"
            << "  Device: " << oro::health::DEVICE_ID << "\n"
            << "═══════════════════════════════════════════════════════\n\n";

  // ── 1. Initialize Storage Handoff ───────────────────────────────────────
  const std::string conn_str =
      "host=localhost user=oro_user password=ogmen dbname=oro_base_db";
  storage_handoff::StorageWriter storage_writer(conn_str);

  // Register the health signals INSERT query
  storage_writer.prepare(
      "insert_signal",
      R"(
      INSERT INTO public.oro_base_signals (
          device_id, dog_id, signal_type,
          signal_value_numeric, signal_value_text, signal_value_boolean,
          unit, observed_at, ingested_at, source, confidence, metadata, created_at
      )
      VALUES (
          $1, $2, $3,
          $4, $5, $6,
          $7, $8, $9, $10, $11, $12::jsonb, NOW()
      )
      )");

  std::cout << "[HealthMonitoringNode] StorageWriter initialized.\n";

  // ── 2. Initialize Health Monitor ────────────────────────────────────────
  HealthMonitor monitor(storage_writer);
  std::cout << "[HealthMonitoringNode] HealthMonitor initialized.\n";

  // ── 3. Load initial config values ───────────────────────────────────────
  {
    uint64_t boot_time = now_ms();

    // Emit initial config-change signals on boot
    monitor.update_timeout_config(15.0, "default", "boot_default", boot_time);
    monitor.update_battery_low_config(20.0, "default", "boot_default",
                                      boot_time);

    // TODO: Load firmware versions from config file
    //       auto firmware_json = oro::health::read_firmware_config(
    //           oro::health::FIRMWARE_CONFIG_PATH);
    //       if (!firmware_json.empty()) {
    //           // Parse JSON and call monitor.update_firmware_version() etc.
    //       }
    std::cout << "[HealthMonitoringNode] Boot config signals emitted.\n";
  }

  // ── 4. Initialize ZMQ Context + Subscriber ──────────────────────────────
  zmq::context_t context(1);
  ZmqSubscriber subscriber(monitor, context,
                            oro::health::SENSOR_IPC_ENDPOINT,
                            oro::health::SYSTEM_IPC_ENDPOINT,
                            oro::health::STATUS_IPC_ENDPOINT);
  subscriber.start();

  // ── 5. Main Tick Loop ───────────────────────────────────────────────────
  std::cout << "[HealthMonitoringNode] Entering main tick loop "
            << "(interval: " << oro::health::TICK_SLEEP_MS << "ms)...\n\n";

  while (g_running.load(std::memory_order_relaxed)) {
    uint64_t current_time = now_ms();
    monitor.tick(current_time);
    std::this_thread::sleep_for(
        std::chrono::milliseconds(oro::health::TICK_SLEEP_MS));
  }

  // ── 6. Graceful Shutdown ────────────────────────────────────────────────
  subscriber.stop();

  std::cout << "\n═══════════════════════════════════════════════════════\n"
            << "  Health Monitoring Node shutdown complete.\n"
            << "═══════════════════════════════════════════════════════\n";
  return 0;
}
