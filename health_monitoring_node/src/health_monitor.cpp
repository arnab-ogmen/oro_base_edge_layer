#include "health_monitor.hpp"
#include <cmath>

HealthMonitor::HealthMonitor(storage_handoff::StorageWriter &writer)
    : writer_(writer) {}

void HealthMonitor::emit_signal(const SignalRecord &record) {
  writer_.insert_signal(record);
}

void HealthMonitor::tick(uint64_t current_time_ms) {
  // Evaluate if any periodic signals are due for an emission
  // Note: In some designs, periodic emission only happens when data is present.
  // Here we assume if tick() is called and time elapsed, we might push.
  // However, usually we emit on data arrival if it's periodic-upon-sampling.
  // The data arrival methods will also check the intervals.
}

void HealthMonitor::update_device_heartbeat(uint64_t current_time_ms) {
  if (current_time_ms - last_heartbeat_emit_ >= HEARTBEAT_INTERVAL_MS) {
    SignalRecord rec;
    rec.device_id = "radxa-host-01";
    rec.signal_type = "device_heartbeat";
    rec.signal_value_boolean = true;
    rec.unit = "none";
    rec.observed_at = current_time_ms;
    rec.ingested_at = current_time_ms;

    emit_signal(rec);
    last_heartbeat_emit_ = current_time_ms;
  }
}

void HealthMonitor::update_battery_level(double level,
                                         uint64_t current_time_ms) {
  bool time_elapsed =
      (current_time_ms - last_battery_emit_ >= BATTERY_INTERVAL_MS);
  bool significant_change =
      (last_battery_level_ < 0) ||
      (std::abs(level - last_battery_level_) >= BATTERY_CHANGE_THRESHOLD);

  if (time_elapsed || significant_change) {
    SignalRecord rec;
    rec.device_id = "radxa-host-01";
    rec.signal_type = "battery_level";
    rec.signal_value_numeric = level;
    rec.unit = "percent";
    rec.observed_at = current_time_ms;
    rec.ingested_at = current_time_ms;

    emit_signal(rec);
    last_battery_level_ = level;
    last_battery_emit_ = current_time_ms;
  }
}

void HealthMonitor::update_frame_quality(double quality,
                                         uint64_t current_time_ms) {
  last_frame_quality_ = quality; // Keep latest
  if (current_time_ms - last_frame_quality_emit_ >= FRAME_QUALITY_INTERVAL_MS) {
    SignalRecord rec;
    rec.device_id = "radxa-host-01";
    rec.signal_type = "frame_brightness_contrast_quality";
    rec.signal_value_numeric = quality;
    rec.unit = "score";
    rec.observed_at = current_time_ms;
    rec.ingested_at = current_time_ms;

    emit_signal(rec);
    last_frame_quality_emit_ = current_time_ms;
  }
}

void HealthMonitor::update_device_connectivity_status(
    const std::string &status, uint64_t current_time_ms) {
  if (status != last_connectivity_status_) {
    SignalRecord rec;
    rec.device_id = "radxa-host-01";
    rec.signal_type = "device_connectivity_status";
    rec.signal_value_text = status;
    rec.unit = "none";
    rec.observed_at = current_time_ms;
    rec.ingested_at = current_time_ms;

    emit_signal(rec);
    last_connectivity_status_ = status;
  }
}

void HealthMonitor::update_power_supply_status(const std::string &status,
                                               uint64_t current_time_ms) {
  if (status != last_power_status_) {
    SignalRecord rec;
    rec.device_id = "radxa-host-01";
    rec.signal_type = "power_supply_status";
    rec.signal_value_text = status;
    rec.unit = "none";
    rec.observed_at = current_time_ms;
    rec.ingested_at = current_time_ms;

    emit_signal(rec);
    last_power_status_ = status;
  }
}

void HealthMonitor::update_camera_obstruction_status(bool obstructed,
                                                     uint64_t current_time_ms) {
  if (!has_last_camera_obstruction_ || last_camera_obstruction_ != obstructed) {
    SignalRecord rec;
    rec.device_id = "radxa-host-01";
    rec.signal_type = "camera_obstruction_status";
    rec.signal_value_boolean = obstructed;
    rec.unit = "none";
    rec.observed_at = current_time_ms;
    rec.ingested_at = current_time_ms;

    emit_signal(rec);
    last_camera_obstruction_ = obstructed;
    has_last_camera_obstruction_ = true;
  }
}

void HealthMonitor::update_sensor_health_status(const std::string &sensor_id,
                                                const std::string &status,
                                                uint64_t current_time_ms) {
  if (last_sensor_health_[sensor_id] != status) {
    SignalRecord rec;
    rec.device_id = sensor_id;
    rec.signal_type = "sensor_health_status";
    rec.signal_value_text = status;
    rec.unit = "none";
    rec.observed_at = current_time_ms;
    rec.ingested_at = current_time_ms;

    emit_signal(rec);
    last_sensor_health_[sensor_id] = status;
  }
}

void HealthMonitor::update_sensor_communication_status(
    const std::string &sensor_id, const std::string &status,
    uint64_t current_time_ms) {
  if (last_sensor_comm_[sensor_id] != status) {
    SignalRecord rec;
    rec.device_id = sensor_id;
    rec.signal_type = "sensor_communication_status";
    rec.signal_value_text = status;
    rec.unit = "none";
    rec.observed_at = current_time_ms;
    rec.ingested_at = current_time_ms;

    emit_signal(rec);
    last_sensor_comm_[sensor_id] = status;
  }
}

void HealthMonitor::update_timeout_config(double threshold,
                                          uint64_t current_time_ms) {
  if (std::abs(threshold - timeout_config_) > 0.0001) {
    SignalRecord rec;
    rec.device_id = "radxa-host-01";
    rec.signal_type = "communication_timeout_threshold";
    rec.signal_value_numeric = threshold;
    rec.unit = "seconds";
    rec.observed_at = current_time_ms;
    rec.ingested_at = current_time_ms;

    emit_signal(rec);
    timeout_config_ = threshold;
  }
}

void HealthMonitor::update_battery_low_config(double threshold,
                                              uint64_t current_time_ms) {
  if (std::abs(threshold - battery_low_config_) > 0.0001) {
    SignalRecord rec;
    rec.device_id = "radxa-host-01";
    rec.signal_type = "battery_low_threshold";
    rec.signal_value_numeric = threshold;
    rec.unit = "percent";
    rec.observed_at = current_time_ms;
    rec.ingested_at = current_time_ms;

    emit_signal(rec);
    battery_low_config_ = threshold;
  }
}
