#ifndef INCLUDE_HEALTH_MONITOR_HPP
#define INCLUDE_HEALTH_MONITOR_HPP
// ============================================================================
// health_monitor.hpp — Health Monitor Core
//
// Maintains local subsystem state, evaluates emission rules (periodic,
// change-based, config-change, event), builds signal-shaped records with
// enriched metadata, and writes them to the signals table via StorageWriter.
//
// Covers all 21 DHM signals (#71–#83, #98, #106, #107, #110, #127, #128).
// ============================================================================

#include "config.hpp"
#include "signal_record.hpp"
#include "storage_handoff/storage_handoff.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>

class HealthMonitor {
public:
  explicit HealthMonitor(storage_handoff::StorageWriter &writer);
  ~HealthMonitor() = default;

  /// Called periodically from the main loop to trigger time-based emissions
  /// (heartbeat, battery snapshot, frame quality, last_seen).
  void tick(uint64_t current_time_ms);

  // ═══════════════════════════════════════════════════════════════════════════
  // PERIODIC SIGNALS
  // ═══════════════════════════════════════════════════════════════════════════

  /// #71 device_heartbeat — Called when a heartbeat is received via ZMQ.
  /// Emits to DB every HEARTBEAT_INTERVAL_MS.
  /// @param seq_num          Sequence number from the heartbeat message
  /// @param current_time_ms  Current system time
  void update_device_heartbeat(uint8_t seq_num, uint64_t current_time_ms);

  /// #76 frame_brightness_contrast_quality — Periodic sampled update.
  /// Stores latest quality and emits at FRAME_QUALITY_INTERVAL_MS.
  /// @param quality  Quality score from camera subsystem
  /// @param camera_id  Camera identifier (e.g., "cam_0")
  /// @param frame_id   Frame identifier if available
  void update_frame_quality(double quality, const std::string &camera_id,
                            const std::string &frame_id,
                            uint64_t current_time_ms);

  // ═══════════════════════════════════════════════════════════════════════════
  // HYBRID SIGNALS (Periodic + Threshold Change)
  // ═══════════════════════════════════════════════════════════════════════════

  /// #73 battery_level — Emit on ≥1% change OR every
  /// BATTERY_SNAPSHOT_INTERVAL_MS.
  /// @param level       Battery percentage (0–100)
  /// @param battery_id  Battery identifier
  /// @param power_mode  Current power mode (e.g., "normal", "low_power")
  void update_battery_level(double level, const std::string &battery_id,
                            const std::string &power_mode,
                            uint64_t current_time_ms);

  // ═══════════════════════════════════════════════════════════════════════════
  // CHANGE-BASED SIGNALS
  // ═══════════════════════════════════════════════════════════════════════════

  /// #72 device_connectivity_status — Emit only on state change.
  /// @param status          Categorical: "connected", "disconnected", etc.
  /// @param network_type    e.g., "wifi", "ethernet"
  /// @param signal_strength Signal strength (dBm or similar)
  void update_device_connectivity_status(const std::string &status,
                                         const std::string &network_type,
                                         const std::string &signal_strength,
                                         uint64_t current_time_ms);

  /// #74 power_supply_status — Emit only on state change.
  /// @param status       Categorical: "on_mains", "on_battery", "charging"
  /// @param source_type  e.g., "mains", "battery", "usb"
  /// @param voltage_state Voltage state description
  void update_power_supply_status(const std::string &status,
                                  const std::string &source_type,
                                  const std::string &voltage_state,
                                  uint64_t current_time_ms);

  /// #75 camera_obstruction_status — Emit only on state change.
  /// @param obstructed      Whether camera is obstructed
  /// @param camera_id       Camera identifier
  /// @param confidence_score Detection confidence (0.0–1.0)
  void update_camera_obstruction_status(bool obstructed,
                                        const std::string &camera_id,
                                        double confidence_score,
                                        uint64_t current_time_ms);

  /// #77 sensor_health_status — Emit only on state change (per sensor).
  /// @param sensor_id       Sensor identifier
  /// @param status          Categorical: "healthy", "degraded", "failed"
  /// @param diagnostic_code Diagnostic code for debugging
  void update_sensor_health_status(const std::string &sensor_id,
                                   const std::string &status,
                                   const std::string &diagnostic_code,
                                   uint64_t current_time_ms);

  /// #78 sensor_communication_status — Emit only on state change (per sensor).
  /// @param sensor_id       Sensor identifier
  /// @param status          Categorical: "healthy", "warning", "critical"
  /// @param bus_interface   Communication bus (e.g., "i2c", "uart", "spi")
  void update_sensor_communication_status(const std::string &sensor_id,
                                          const std::string &status,
                                          const std::string &bus_interface,
                                          uint64_t current_time_ms);

  /// #81 firmware_update_availability_flag — Emit on change.
  /// @param component_name  e.g., "mcu", "host"
  /// @param available       Whether an update is available
  /// @param current_version Current installed firmware version
  void update_firmware_update_available(const std::string &component_name,
                                        bool available,
                                        const std::string &current_version,
                                        uint64_t current_time_ms);

  /// #83 firmware_update_status — Emit on state change.
  /// @param component_name   e.g., "mcu", "host"
  /// @param status           Categorical: "idle", "downloading", "installing",
  /// "complete", "failed"
  /// @param progress_percent Progress (0–100)
  void update_firmware_update_status(const std::string &component_name,
                                     const std::string &status,
                                     double progress_percent,
                                     uint64_t current_time_ms);

  /// #127 fountain_pump_health_status — Emit on state change.
  /// Derived from /status/water_pump ZMQ topic.
  /// @param pump_id         Pump identifier
  /// @param status          Categorical: "healthy", "degraded", "failed", "off"
  /// @param diagnostic_code Diagnostic code
  void update_fountain_pump_health(const std::string &pump_id,
                                   const std::string &status,
                                   const std::string &diagnostic_code,
                                   uint64_t current_time_ms);

  /// #128 water_fountain_status — Emit on state change.
  /// Derived from /status/water_pump ZMQ topic.
  /// @param fountain_id    Fountain identifier
  /// @param status         Categorical: "active", "idle", "error"
  /// @param operating_mode Operating mode description
  void update_water_fountain_status(const std::string &fountain_id,
                                    const std::string &status,
                                    const std::string &operating_mode,
                                    uint64_t current_time_ms);

  /// #127 & #128 — Unified pump state update.
  /// @param status "running" or "idle"
  void update_water_pump_state(const std::string &status,
                               uint64_t current_time_ms);

  /// Helper for bowl water level sensor.
  /// @param state 0=Not Full, 1=Full
  void update_bowl_water_level(int state, uint64_t current_time_ms);

  // ═══════════════════════════════════════════════════════════════════════════
  // CONFIG-CHANGE SIGNALS
  // ═══════════════════════════════════════════════════════════════════════════

  /// #79 firmware_version — Emit on config load/change.
  /// Read from firmware config file.
  /// @param component_name  e.g., "mcu", "host"
  /// @param version         Firmware version string
  /// @param build_id        Build identifier
  void update_firmware_version(const std::string &component_name,
                               const std::string &version,
                               const std::string &build_id,
                               uint64_t current_time_ms);

  /// #80 latest_firmware_version_available — Emit on config load/change.
  /// @param component_name  e.g., "mcu", "host"
  /// @param version         Latest available version string
  /// @param source          Where version info was fetched from
  void update_latest_firmware_available(const std::string &component_name,
                                        const std::string &version,
                                        const std::string &source,
                                        uint64_t current_time_ms);

  /// #107 communication_timeout_threshold — Emit on config load/change.
  /// @param threshold       Timeout value in seconds
  /// @param component_name  Component this threshold applies to
  /// @param configured_by   Who/what set this value
  void update_timeout_config(double threshold,
                             const std::string &component_name,
                             const std::string &configured_by,
                             uint64_t current_time_ms);

  /// #110 battery_low_threshold — Emit on config load/change.
  /// @param threshold      Low battery threshold (percentage)
  /// @param battery_id     Battery identifier
  /// @param configured_by  Who/what set this value
  void update_battery_low_config(double threshold,
                                 const std::string &battery_id,
                                 const std::string &configured_by,
                                 uint64_t current_time_ms);

  // ═══════════════════════════════════════════════════════════════════════════
  // EVENT SIGNALS (emit once per occurrence)
  // ═══════════════════════════════════════════════════════════════════════════

  /// #82 firmware_update_completion_event — Emit once when update completes.
  /// @param component_name  e.g., "mcu", "host"
  /// @param target_version  Version that was installed
  void emit_firmware_update_complete(const std::string &component_name,
                                     const std::string &target_version,
                                     uint64_t current_time_ms);

  // ═══════════════════════════════════════════════════════════════════════════
  // TODO / PLACEHOLDER SIGNALS
  // ═══════════════════════════════════════════════════════════════════════════

  // TODO: #98 settings_apply_success_status — Needs clarity on ownership
  //       (health node observing /commands/settings/apply ACK vs command
  //       executor). Placeholder: logs a message but does not emit a signal.
  void update_settings_apply_status(const std::string &settings_profile_id,
                                    bool success,
                                    const std::string &failure_reason,
                                    uint64_t current_time_ms);

  // TODO: #106 last_seen_timestamp — Needs discussion on emission strategy.
  //       Placeholder: tracks timestamps but does not emit signals yet.
  void update_last_seen(const std::string &entity_type,
                        const std::string &entity_id, uint64_t current_time_ms);

private:
  void emit_signal(const SignalRecord &record);

  /// Helper to get current time as Unix epoch milliseconds
  static uint64_t now_ms();

  struct NetworkInfo {
    std::string type;
    std::string strength;
  };
  NetworkInfo get_system_network_info();

  storage_handoff::StorageWriter &writer_;

  // ── State Cache & Emission Timers ─────────────────────────────────────────

  // #71 device_heartbeat — Periodic (Node & MCU)
  uint64_t last_node_heartbeat_emit_ = 0;
  uint64_t node_heartbeat_seq_ = 0;

  uint64_t last_hb_arrival_time_ms_ = 0;
  uint64_t last_hb_change_time_ms_ = 0;
  uint8_t last_hb_seq_ = 0;
  bool hb_stale_ = false;
  uint64_t node_start_time_ms_ = 0;

  // #73 battery_level — Hybrid
  double last_battery_level_ = -1.0;
  uint64_t last_battery_emit_ = 0;
  bool battery_low_event_logged_ = false;

  // #76 frame_brightness_contrast_quality — Periodic
  double last_frame_quality_ = -1.0;
  uint64_t last_frame_quality_emit_ = 0;

  // #72 device_connectivity_status — Change-based
  std::string last_connectivity_status_;

  // #74 power_supply_status — Change-based
  std::string last_power_status_;
  std::string last_power_source_type_;
  std::string last_power_voltage_state_;
  uint64_t last_power_emit_ms_ = 0;

  // #75 camera_obstruction_status — Change-based
  bool has_last_camera_obstruction_ = false;
  bool last_camera_obstruction_ = false;

  // #77 sensor_health_status — Change-based (per sensor)
  std::unordered_map<std::string, std::string> last_sensor_health_;

  // #78 sensor_communication_status — Change-based (per sensor)
  std::unordered_map<std::string, std::string> last_sensor_comm_;

  // #79 firmware_version — Config-change (per component)
  std::unordered_map<std::string, std::string> firmware_versions_;

  // #80 latest_firmware_version_available — Config-change (per component)
  std::unordered_map<std::string, std::string> latest_firmware_available_;

  // #81 firmware_update_availability_flag — Change-based (per component)
  std::unordered_map<std::string, bool> firmware_update_flags_;

  // #83 firmware_update_status — Change-based (per component)
  std::unordered_map<std::string, std::string> firmware_update_statuses_;

  // #107 communication_timeout_threshold — Config-change
  double timeout_config_ = -1.0;

  // #110 battery_low_threshold — Config-change
  double battery_low_config_ = -1.0;

  // #127 fountain_pump_health_status — Change-based (per pump)
  std::unordered_map<std::string, std::string> fountain_pump_health_;

  // #128 water_fountain_status — Change-based (per fountain)
  std::unordered_map<std::string, std::string> water_fountain_status_;

  // Pump state tracking for #127 and #128
  std::string last_pump_state_ = "unknown";
  uint64_t pump_on_start_time_ms_ = 0;
  bool is_pump_running_ = false;
  bool pump_damage_reported_ = false;
  int last_bowl_level_ = 0; // 0=Not Full, 1=Full

  // #106 last_seen_timestamp — TODO: placeholder tracking
  std::unordered_map<std::string, uint64_t> last_seen_timestamps_;
  uint64_t last_seen_emit_time_ = 0;
};
#endif // INCLUDE_HEALTH_MONITOR_HPP
