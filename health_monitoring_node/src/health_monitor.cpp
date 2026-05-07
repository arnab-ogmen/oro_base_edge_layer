#include "health_monitor.hpp"

#include <chrono>
#include <cmath>
#include <iostream>

// ═════════════════════════════════════════════════════════════════════════════
// Construction & Helpers
// ═════════════════════════════════════════════════════════════════════════════

HealthMonitor::HealthMonitor(storage_handoff::StorageWriter &writer)
    : writer_(writer) {}

void HealthMonitor::emit_signal(const SignalRecord &record) {
  writer_.insert_signal(record);
}

uint64_t HealthMonitor::now_ms() {
  auto now = std::chrono::system_clock::now();
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          now.time_since_epoch())
          .count());
}

// ═════════════════════════════════════════════════════════════════════════════
// Tick — Periodic Emission Controller
// ═════════════════════════════════════════════════════════════════════════════

void HealthMonitor::tick(uint64_t current_time_ms) {
  // #71 device_heartbeat — always emit on tick if interval elapsed
  update_device_heartbeat(current_time_ms);

  // #73 battery_level — periodic snapshot (if we have a cached value)
  if (last_battery_level_ >= 0.0) {
    bool time_elapsed =
        (current_time_ms - last_battery_emit_ >=
         oro::health::BATTERY_SNAPSHOT_INTERVAL_MS);
    if (time_elapsed) {
      // Force periodic snapshot emission with last known value
      SignalRecord rec;
      rec.device_id = oro::health::DEVICE_ID;
      rec.signal_type = "battery_level";
      rec.signal_value_numeric = last_battery_level_;
      rec.unit = "percent";
      rec.observed_at = current_time_ms;
      rec.ingested_at = now_ms();
      rec.source = "system";
      rec.metadata = R"({"battery_id":"default","power_mode":"unknown")"
                     R"(,"snapshot_reason":"periodic"})";
      emit_signal(rec);
      last_battery_emit_ = current_time_ms;
    }
  }

  // #76 frame_brightness_contrast_quality — periodic snapshot
  if (last_frame_quality_ >= 0.0) {
    if (current_time_ms - last_frame_quality_emit_ >=
        oro::health::FRAME_QUALITY_INTERVAL_MS) {
      SignalRecord rec;
      rec.device_id = oro::health::DEVICE_ID;
      rec.signal_type = "frame_brightness_contrast_quality";
      rec.signal_value_numeric = last_frame_quality_;
      rec.unit = "score";
      rec.observed_at = current_time_ms;
      rec.ingested_at = now_ms();
      rec.source = "system";
      rec.metadata =
          R"({"camera_id":"default","frame_id":"","snapshot_reason":"periodic"})";
      emit_signal(rec);
      last_frame_quality_emit_ = current_time_ms;
    }
  }
}

// ═════════════════════════════════════════════════════════════════════════════
// #71 — device_heartbeat (Periodic)
// ═════════════════════════════════════════════════════════════════════════════

void HealthMonitor::update_device_heartbeat(uint64_t current_time_ms) {
  if (current_time_ms - last_heartbeat_emit_ >=
      oro::health::HEARTBEAT_INTERVAL_MS) {
    ++heartbeat_seq_;

    SignalRecord rec;
    rec.device_id = oro::health::DEVICE_ID;
    rec.signal_type = "device_heartbeat";
    rec.signal_value_boolean = true;
    rec.unit = "none";
    rec.observed_at = current_time_ms;
    rec.ingested_at = now_ms();
    rec.source = "system";
    rec.metadata = R"({"heartbeat_seq":)" + std::to_string(heartbeat_seq_) +
                   R"(,"runtime_uptime_sec":)" +
                   std::to_string(current_time_ms / 1000) + "}";

    emit_signal(rec);
    last_heartbeat_emit_ = current_time_ms;
  }
}

// ═════════════════════════════════════════════════════════════════════════════
// #73 — battery_level (Hybrid: Periodic + Threshold Change)
// ═════════════════════════════════════════════════════════════════════════════

void HealthMonitor::update_battery_level(double level,
                                         const std::string &battery_id,
                                         const std::string &power_mode,
                                         uint64_t current_time_ms) {
  bool time_elapsed =
      (current_time_ms - last_battery_emit_ >=
       oro::health::BATTERY_SNAPSHOT_INTERVAL_MS);
  bool significant_change =
      (last_battery_level_ < 0) ||
      (std::abs(level - last_battery_level_) >=
       oro::health::BATTERY_CHANGE_THRESHOLD);

  if (time_elapsed || significant_change) {
    SignalRecord rec;
    rec.device_id = oro::health::DEVICE_ID;
    rec.signal_type = "battery_level";
    rec.signal_value_numeric = level;
    rec.unit = "percent";
    rec.observed_at = current_time_ms;
    rec.ingested_at = now_ms();
    rec.source = "system";
    rec.metadata = R"({"battery_id":")" + battery_id +
                   R"(","power_mode":")" + power_mode +
                   R"(","threshold":)" +
                   std::to_string(oro::health::BATTERY_CHANGE_THRESHOLD) + "}";

    emit_signal(rec);
    last_battery_level_ = level;
    last_battery_emit_ = current_time_ms;
  }
}

// ═════════════════════════════════════════════════════════════════════════════
// #76 — frame_brightness_contrast_quality (Periodic)
// ═════════════════════════════════════════════════════════════════════════════

void HealthMonitor::update_frame_quality(double quality,
                                         const std::string &camera_id,
                                         const std::string &frame_id,
                                         uint64_t current_time_ms) {
  last_frame_quality_ = quality; // Always cache latest value
  if (current_time_ms - last_frame_quality_emit_ >=
      oro::health::FRAME_QUALITY_INTERVAL_MS) {
    SignalRecord rec;
    rec.device_id = oro::health::DEVICE_ID;
    rec.signal_type = "frame_brightness_contrast_quality";
    rec.signal_value_numeric = quality;
    rec.unit = "score";
    rec.observed_at = current_time_ms;
    rec.ingested_at = now_ms();
    rec.source = "system";
    rec.metadata = R"({"camera_id":")" + camera_id + R"(","frame_id":")" +
                   frame_id + R"("})";

    emit_signal(rec);
    last_frame_quality_emit_ = current_time_ms;
  }
}

// ═════════════════════════════════════════════════════════════════════════════
// #72 — device_connectivity_status (Change-based)
// ═════════════════════════════════════════════════════════════════════════════

void HealthMonitor::update_device_connectivity_status(
    const std::string &status, const std::string &network_type,
    const std::string &signal_strength, uint64_t current_time_ms) {
  if (status != last_connectivity_status_) {
    SignalRecord rec;
    rec.device_id = oro::health::DEVICE_ID;
    rec.signal_type = "device_connectivity_status";
    rec.signal_value_text = status;
    rec.unit = "categorical";
    rec.observed_at = current_time_ms;
    rec.ingested_at = now_ms();
    rec.source = "system";
    rec.metadata = R"({"network_type":")" + network_type +
                   R"(","signal_strength":")" + signal_strength +
                   R"(","reason_code":""})";

    emit_signal(rec);
    last_connectivity_status_ = status;
  }
}

// ═════════════════════════════════════════════════════════════════════════════
// #74 — power_supply_status (Change-based)
// ═════════════════════════════════════════════════════════════════════════════

void HealthMonitor::update_power_supply_status(const std::string &status,
                                               const std::string &source_type,
                                               const std::string &voltage_state,
                                               uint64_t current_time_ms) {
  if (status != last_power_status_) {
    SignalRecord rec;
    rec.device_id = oro::health::DEVICE_ID;
    rec.signal_type = "power_supply_status";
    rec.signal_value_text = status;
    rec.unit = "categorical";
    rec.observed_at = current_time_ms;
    rec.ingested_at = now_ms();
    rec.source = "system";
    rec.metadata = R"({"source_type":")" + source_type +
                   R"(","voltage_state":")" + voltage_state +
                   R"(","reason_code":""})";

    emit_signal(rec);
    last_power_status_ = status;
  }
}

// ═════════════════════════════════════════════════════════════════════════════
// #75 — camera_obstruction_status (Change-based)
// ═════════════════════════════════════════════════════════════════════════════

void HealthMonitor::update_camera_obstruction_status(
    bool obstructed, const std::string &camera_id, double confidence_score,
    uint64_t current_time_ms) {
  if (!has_last_camera_obstruction_ ||
      last_camera_obstruction_ != obstructed) {
    SignalRecord rec;
    rec.device_id = oro::health::DEVICE_ID;
    rec.signal_type = "camera_obstruction_status";
    rec.signal_value_boolean = obstructed;
    rec.unit = "boolean";
    rec.observed_at = current_time_ms;
    rec.ingested_at = now_ms();
    rec.source = "system";
    rec.confidence = confidence_score;
    rec.metadata = R"({"camera_id":")" + camera_id +
                   R"(","reason_code":"","frame_id":""})";

    emit_signal(rec);
    last_camera_obstruction_ = obstructed;
    has_last_camera_obstruction_ = true;
  }
}

// ═════════════════════════════════════════════════════════════════════════════
// #77 — sensor_health_status (Change-based, per sensor)
// ═════════════════════════════════════════════════════════════════════════════

void HealthMonitor::update_sensor_health_status(
    const std::string &sensor_id, const std::string &status,
    const std::string &diagnostic_code, uint64_t current_time_ms) {
  if (last_sensor_health_[sensor_id] != status) {
    SignalRecord rec;
    rec.device_id = oro::health::DEVICE_ID;
    rec.signal_type = "sensor_health_status";
    rec.signal_value_text = status;
    rec.unit = "categorical";
    rec.observed_at = current_time_ms;
    rec.ingested_at = now_ms();
    rec.source = "system";
    rec.metadata = R"({"sensor_id":")" + sensor_id +
                   R"(","diagnostic_code":")" + diagnostic_code +
                   R"(","reason_code":""})";

    emit_signal(rec);
    last_sensor_health_[sensor_id] = status;
  }
}

// ═════════════════════════════════════════════════════════════════════════════
// #78 — sensor_communication_status (Change-based, per sensor)
// ═════════════════════════════════════════════════════════════════════════════

void HealthMonitor::update_sensor_communication_status(
    const std::string &sensor_id, const std::string &status,
    const std::string &bus_interface, uint64_t current_time_ms) {
  if (last_sensor_comm_[sensor_id] != status) {
    SignalRecord rec;
    rec.device_id = oro::health::DEVICE_ID;
    rec.signal_type = "sensor_communication_status";
    rec.signal_value_text = status;
    rec.unit = "categorical";
    rec.observed_at = current_time_ms;
    rec.ingested_at = now_ms();
    rec.source = "system";
    rec.metadata = R"({"sensor_id":")" + sensor_id +
                   R"(","bus_interface":")" + bus_interface +
                   R"(","reason_code":""})";

    emit_signal(rec);
    last_sensor_comm_[sensor_id] = status;
  }
}

// ═════════════════════════════════════════════════════════════════════════════
// #79 — firmware_version (Config-change, per component)
// ═════════════════════════════════════════════════════════════════════════════

void HealthMonitor::update_firmware_version(const std::string &component_name,
                                            const std::string &version,
                                            const std::string &build_id,
                                            uint64_t current_time_ms) {
  if (firmware_versions_[component_name] != version) {
    SignalRecord rec;
    rec.device_id = oro::health::DEVICE_ID;
    rec.signal_type = "firmware_version";
    rec.signal_value_text = version;
    rec.unit = "string";
    rec.observed_at = current_time_ms;
    rec.ingested_at = now_ms();
    rec.source = "system";
    rec.metadata = R"({"component_name":")" + component_name +
                   R"(","build_id":")" + build_id + R"("})";

    emit_signal(rec);
    firmware_versions_[component_name] = version;
  }
}

// ═════════════════════════════════════════════════════════════════════════════
// #80 — latest_firmware_version_available (Config-change, per component)
// ═════════════════════════════════════════════════════════════════════════════

void HealthMonitor::update_latest_firmware_available(
    const std::string &component_name, const std::string &version,
    const std::string &source, uint64_t current_time_ms) {
  if (latest_firmware_available_[component_name] != version) {
    SignalRecord rec;
    rec.device_id = oro::health::DEVICE_ID;
    rec.signal_type = "latest_firmware_version_available";
    rec.signal_value_text = version;
    rec.unit = "string";
    rec.observed_at = current_time_ms;
    rec.ingested_at = now_ms();
    rec.source = "system";
    rec.metadata = R"({"component_name":")" + component_name +
                   R"(","source":")" + source + R"("})";

    emit_signal(rec);
    latest_firmware_available_[component_name] = version;
  }
}

// ═════════════════════════════════════════════════════════════════════════════
// #81 — firmware_update_availability_flag (Change-based, per component)
// ═════════════════════════════════════════════════════════════════════════════

void HealthMonitor::update_firmware_update_available(
    const std::string &component_name, bool available,
    const std::string &current_version, uint64_t current_time_ms) {
  auto it = firmware_update_flags_.find(component_name);
  if (it == firmware_update_flags_.end() || it->second != available) {
    SignalRecord rec;
    rec.device_id = oro::health::DEVICE_ID;
    rec.signal_type = "firmware_update_availability_flag";
    rec.signal_value_boolean = available;
    rec.unit = "boolean";
    rec.observed_at = current_time_ms;
    rec.ingested_at = now_ms();
    rec.source = "system";
    rec.metadata = R"({"component_name":")" + component_name +
                   R"(","current_version":")" + current_version + R"("})";

    emit_signal(rec);
    firmware_update_flags_[component_name] = available;
  }
}

// ═════════════════════════════════════════════════════════════════════════════
// #82 — firmware_update_completion_event (Event — emit once)
// ═════════════════════════════════════════════════════════════════════════════

void HealthMonitor::emit_firmware_update_complete(
    const std::string &component_name, const std::string &target_version,
    uint64_t current_time_ms) {
  // Events always emit — no state-change gating.
  SignalRecord rec;
  rec.device_id = oro::health::DEVICE_ID;
  rec.signal_type = "firmware_update_completion_event";
  rec.signal_value_text = "completed";
  rec.unit = "event";
  rec.observed_at = current_time_ms;
  rec.ingested_at = now_ms();
  rec.source = "system";
  rec.metadata = R"({"component_name":")" + component_name +
                 R"(","target_version":")" + target_version + R"("})";

  emit_signal(rec);
}

// ═════════════════════════════════════════════════════════════════════════════
// #83 — firmware_update_status (Change-based, per component)
// ═════════════════════════════════════════════════════════════════════════════

void HealthMonitor::update_firmware_update_status(
    const std::string &component_name, const std::string &status,
    double progress_percent, uint64_t current_time_ms) {
  if (firmware_update_statuses_[component_name] != status) {
    SignalRecord rec;
    rec.device_id = oro::health::DEVICE_ID;
    rec.signal_type = "firmware_update_status";
    rec.signal_value_text = status;
    rec.unit = "categorical";
    rec.observed_at = current_time_ms;
    rec.ingested_at = now_ms();
    rec.source = "system";
    rec.metadata = R"({"component_name":")" + component_name +
                   R"(","progress_percent":)" +
                   std::to_string(progress_percent) + "}";

    emit_signal(rec);
    firmware_update_statuses_[component_name] = status;
  }
}

// ═════════════════════════════════════════════════════════════════════════════
// #107 — communication_timeout_threshold (Config-change)
// ═════════════════════════════════════════════════════════════════════════════

void HealthMonitor::update_timeout_config(double threshold,
                                          const std::string &component_name,
                                          const std::string &configured_by,
                                          uint64_t current_time_ms) {
  if (std::abs(threshold - timeout_config_) > 0.0001) {
    SignalRecord rec;
    rec.device_id = oro::health::DEVICE_ID;
    rec.signal_type = "communication_timeout_threshold";
    rec.signal_value_numeric = threshold;
    rec.unit = "seconds";
    rec.observed_at = current_time_ms;
    rec.ingested_at = now_ms();
    rec.source = "system";
    rec.metadata = R"({"component_name":")" + component_name +
                   R"(","configured_by":")" + configured_by +
                   R"(","config_version":"1"})";

    emit_signal(rec);
    timeout_config_ = threshold;
  }
}

// ═════════════════════════════════════════════════════════════════════════════
// #110 — battery_low_threshold (Config-change)
// ═════════════════════════════════════════════════════════════════════════════

void HealthMonitor::update_battery_low_config(double threshold,
                                              const std::string &battery_id,
                                              const std::string &configured_by,
                                              uint64_t current_time_ms) {
  if (std::abs(threshold - battery_low_config_) > 0.0001) {
    SignalRecord rec;
    rec.device_id = oro::health::DEVICE_ID;
    rec.signal_type = "battery_low_threshold";
    rec.signal_value_numeric = threshold;
    rec.unit = "percent";
    rec.observed_at = current_time_ms;
    rec.ingested_at = now_ms();
    rec.source = "system";
    rec.metadata = R"({"battery_id":")" + battery_id +
                   R"(","configured_by":")" + configured_by +
                   R"(","config_version":"1"})";

    emit_signal(rec);
    battery_low_config_ = threshold;
  }
}

// ═════════════════════════════════════════════════════════════════════════════
// #127 — fountain_pump_health_status (Change-based, per pump)
// ═════════════════════════════════════════════════════════════════════════════

void HealthMonitor::update_fountain_pump_health(
    const std::string &pump_id, const std::string &status,
    const std::string &diagnostic_code, uint64_t current_time_ms) {
  if (fountain_pump_health_[pump_id] != status) {
    SignalRecord rec;
    rec.device_id = oro::health::DEVICE_ID;
    rec.signal_type = "fountain_pump_health_status";
    rec.signal_value_text = status;
    rec.unit = "categorical";
    rec.observed_at = current_time_ms;
    rec.ingested_at = now_ms();
    rec.source = "system";
    rec.metadata = R"({"pump_id":")" + pump_id +
                   R"(","diagnostic_code":")" + diagnostic_code + R"("})";

    emit_signal(rec);
    fountain_pump_health_[pump_id] = status;
  }
}

// ═════════════════════════════════════════════════════════════════════════════
// #128 — water_fountain_status (Change-based, per fountain)
// ═════════════════════════════════════════════════════════════════════════════

void HealthMonitor::update_water_fountain_status(
    const std::string &fountain_id, const std::string &status,
    const std::string &operating_mode, uint64_t current_time_ms) {
  if (water_fountain_status_[fountain_id] != status) {
    SignalRecord rec;
    rec.device_id = oro::health::DEVICE_ID;
    rec.signal_type = "water_fountain_status";
    rec.signal_value_text = status;
    rec.unit = "categorical";
    rec.observed_at = current_time_ms;
    rec.ingested_at = now_ms();
    rec.source = "system";
    rec.metadata = R"({"fountain_id":")" + fountain_id +
                   R"(","operating_mode":")" + operating_mode + R"("})";

    emit_signal(rec);
    water_fountain_status_[fountain_id] = status;
  }
}

// ═════════════════════════════════════════════════════════════════════════════
// #98 — settings_apply_success_status (TODO: needs ownership clarity)
// ═════════════════════════════════════════════════════════════════════════════

void HealthMonitor::update_settings_apply_status(
    const std::string &settings_profile_id, bool success,
    const std::string &failure_reason, uint64_t current_time_ms) {
  // TODO: Implement once ownership is clarified.
  //       Options:
  //       - Health node observes /commands/settings/apply and its ACK
  //       - Command executor publishes a result that the health node captures
  //       For now, log the call but do not emit a signal.
  (void)current_time_ms;
  std::cout << "[HealthMonitor] TODO #98 settings_apply_success_status: "
            << "profile=" << settings_profile_id
            << " success=" << (success ? "true" : "false")
            << " failure_reason=" << failure_reason << "\n";
}

// ═════════════════════════════════════════════════════════════════════════════
// #106 — last_seen_timestamp (TODO: needs discussion on emission strategy)
// ═════════════════════════════════════════════════════════════════════════════

void HealthMonitor::update_last_seen(const std::string &entity_type,
                                     const std::string &entity_id,
                                     uint64_t current_time_ms) {
  // TODO: Implement signal emission once strategy is decided.
  //       Currently tracks timestamps internally but does not write signals.
  //       Possible strategies:
  //       - Periodic emission (every LAST_SEEN_INTERVAL_MS)
  //       - Emit only when entity transitions to "timed out"
  //       - Emit on every update (high volume, not recommended)
  std::string key = entity_type + ":" + entity_id;
  last_seen_timestamps_[key] = current_time_ms;
}
