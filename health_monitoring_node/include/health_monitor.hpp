#ifndef INCLUDE_HEALTH_MONITOR_HPP
#define INCLUDE_HEALTH_MONITOR_HPP

#include "storage_handoff/signal_record.hpp"
#include "storage_handoff/storage_handoff.hpp"
#include <cstdint>
#include <string>
#include <unordered_map>

class HealthMonitor {
public:
  explicit HealthMonitor(storage_handoff::StorageWriter &writer);
  ~HealthMonitor() = default;

  // Call this periodically to trigger time-based emissions
  void tick(uint64_t current_time_ms);

  // --- Update Methods per Signal ---

  // Periodic
  void update_device_heartbeat(uint64_t current_time_ms);
  void update_frame_quality(double quality, uint64_t current_time_ms);

  // Hybrid (Periodic + Threshold Change)
  void update_battery_level(double level, uint64_t current_time_ms);

  // Change-based
  void update_device_connectivity_status(const std::string &status,
                                         uint64_t current_time_ms);
  void update_power_supply_status(const std::string &status,
                                  uint64_t current_time_ms);
  void update_camera_obstruction_status(bool obstructed,
                                        uint64_t current_time_ms);
  void update_sensor_health_status(const std::string &sensor_id,
                                   const std::string &status,
                                   uint64_t current_time_ms);
  void update_sensor_communication_status(const std::string &sensor_id,
                                          const std::string &status,
                                          uint64_t current_time_ms);

  // Config-change only
  void update_timeout_config(double threshold, uint64_t current_time_ms);
  void update_battery_low_config(double threshold, uint64_t current_time_ms);

private:
  void emit_signal(const SignalRecord &record);

  storage_handoff::StorageWriter &writer_;

  // --- State Cache & Emission Timers ---
  uint64_t last_heartbeat_emit_ = 0;
  const uint64_t HEARTBEAT_INTERVAL_MS = 10000; // 10 seconds

  double last_battery_level_ = -1.0;
  uint64_t last_battery_emit_ = 0;
  const uint64_t BATTERY_INTERVAL_MS = 60000;  // 60 seconds
  const double BATTERY_CHANGE_THRESHOLD = 1.0; // 1% change

  double last_frame_quality_ = -1.0;
  uint64_t last_frame_quality_emit_ = 0;
  const uint64_t FRAME_QUALITY_INTERVAL_MS = 30000; // 30 seconds

  std::string last_connectivity_status_;
  std::string last_power_status_;

  bool has_last_camera_obstruction_ = false;
  bool last_camera_obstruction_ = false;

  // Maps to track multiple sensors
  std::unordered_map<std::string, std::string> last_sensor_health_;
  std::unordered_map<std::string, std::string> last_sensor_comm_;

  double timeout_config_ = -1.0;
  double battery_low_config_ = -1.0;
};
#endif // INCLUDE_HEALTH_MONITOR_HPP
