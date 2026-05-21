#include "health_monitor.hpp"
#include "storage_handoff/storage_handoff.hpp"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

// ?????????????????????????????????????????????????????????????????????????????
// Construction & Helpers
// ?????????????????????????????????????????????????????????????????????????????

HealthMonitor::HealthMonitor(storage_handoff::StorageWriter &writer)
    : writer_(writer), node_start_time_ms_(now_ms()) {}

void HealthMonitor::emit_signal(const SignalRecord &record) {
  int sig_id = 0;
  if (record.signal_type == "device_heartbeat") sig_id = 71;
  else if (record.signal_type == "device_heartbeat_missed_event") sig_id = 71;
  else if (record.signal_type == "device_connectivity_status") sig_id = 72;
  else if (record.signal_type == "battery_level") sig_id = 73;
  else if (record.signal_type == "power_supply_status") sig_id = 74;
  else if (record.signal_type == "camera_obstruction_status") sig_id = 75;
  else if (record.signal_type == "frame_brightness_contrast_quality") sig_id = 76;
  else if (record.signal_type == "sensor_health_status") sig_id = 77;
  else if (record.signal_type == "sensor_communication_status") sig_id = 78;
  else if (record.signal_type == "firmware_version") sig_id = 79;
  else if (record.signal_type == "latest_firmware_version_available") sig_id = 80;
  else if (record.signal_type == "firmware_update_availability_flag") sig_id = 81;
  else if (record.signal_type == "firmware_update_completion_event") sig_id = 82;
  else if (record.signal_type == "firmware_update_status") sig_id = 83;
  else if (record.signal_type == "settings_apply_success_status") sig_id = 98;
  else if (record.signal_type == "last_seen_timestamp") sig_id = 106;
  else if (record.signal_type == "communication_timeout_threshold") sig_id = 107;
  else if (record.signal_type == "battery_low_threshold") sig_id = 110;
  else if (record.signal_type == "fountain_pump_health_status") sig_id = 127;
  else if (record.signal_type == "water_fountain_status") sig_id = 128;

  std::string obs_at =
      storage_handoff::StorageWriter::unix_ms_to_iso8601(record.observed_at);
  std::string ing_at =
      storage_handoff::StorageWriter::unix_ms_to_iso8601(record.ingested_at);

  std::optional<std::string> dog_id_opt;
  if (!record.dog_id.empty())
    dog_id_opt = record.dog_id;

  std::optional<std::string> metadata_opt;
  if (!record.metadata.empty())
    metadata_opt = record.metadata;

  std::optional<std::string> boolean_opt;
  if (record.signal_value_boolean.has_value()) {
    boolean_opt = *record.signal_value_boolean ? "true" : "false";
  }

  writer_.execute_prepared("insert_signal", sig_id, record.device_id, dog_id_opt,
                           record.signal_type, record.signal_value_numeric,
                           record.signal_value_text, boolean_opt, record.unit,
                           obs_at, ing_at, record.source, record.confidence,
                           metadata_opt);
}

uint64_t HealthMonitor::now_ms() {
  auto now = std::chrono::system_clock::now();
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          now.time_since_epoch())
          .count());
}

// =============================================================================
// Tick ?? Periodic Emission Controller
// =============================================================================

void HealthMonitor::tick(uint64_t current_time_ms) {

  // =============================================================================
  // #71 device_heartbeat ?? periodic snapshot (if we have a cached value)
  // =============================================================================
  if (current_time_ms - last_node_heartbeat_emit_ >=
      oro::health::HEARTBEAT_INTERVAL_MS) {
    ++node_heartbeat_seq_;
    SignalRecord rec;
    rec.device_id = oro::health::DEVICE_ID;
    rec.signal_type = "device_heartbeat";
    rec.signal_value_text =
        storage_handoff::StorageWriter::unix_ms_to_iso8601(current_time_ms);
    rec.unit = "timestamp";
    rec.observed_at = current_time_ms;
    rec.ingested_at = now_ms();
    rec.source = "system";
    rec.metadata =
        R"({"event_time":)" + std::to_string(current_time_ms) +
        R"(,"device_id":")" + std::string(oro::health::DEVICE_ID) +
        R"(","uptime_seconds":)" +
        std::to_string((current_time_ms - node_start_time_ms_) / 1000) + "}";

    emit_signal(rec);
    last_node_heartbeat_emit_ = current_time_ms;
  }

  // =============================================================================
  // #71 device_heartbeat_missed_event ?? periodic snapshot (if we have a cached
  // value)
  // =============================================================================
  if (last_hb_arrival_time_ms_ > 0) {
    bool silent = (current_time_ms - last_hb_arrival_time_ms_) >= 3000;
    bool stagnant = (current_time_ms - last_hb_change_time_ms_) >= 3000;

    if ((silent || stagnant) && !hb_stale_) {
      SignalRecord rec;
      rec.device_id = oro::health::DEVICE_ID;
      rec.signal_type = "device_heartbeat_missed_event";
      rec.signal_value_text = silent ? "silent" : "stagnant";
      rec.unit = "event";
      rec.observed_at = current_time_ms;
      rec.ingested_at = now_ms();
      rec.source = "system";
      rec.metadata =
          R"({"event_time":)" + std::to_string(current_time_ms) +
          R"(,"device_id":")" + std::string(oro::health::DEVICE_ID) +
          R"(","uptime_seconds":)" +
          std::to_string((current_time_ms - node_start_time_ms_) / 1000) +
          R"(,"issue":")" + (silent ? "SILENT" : "STAGNANT") + R"("})";
      emit_signal(rec);
      hb_stale_ = true;
      std::cerr << "[HealthMonitor] CRITICAL: Heartbeat "
                << (silent ? "SILENT" : "STAGNANT") << " for >= 3s!\n";
    }
  }

  // ?????????????????????????????????????????????????????????????????????????????
  // #73 battery_level ?? periodic snapshot (if we have a cached value)
  // ?????????????????????????????????????????????????????????????????????????????

  if (last_battery_level_ >= 0.0 &&
      last_battery_level_ < oro::health::BATTERY_LOW_THRESHOLD) {
    // #110 battery_low_threshold â Emit ONLY ONCE when battery is low
    if (!battery_low_event_logged_) {
      SignalRecord rec;
      rec.device_id = oro::health::DEVICE_ID;
      rec.signal_type = "battery_low_threshold";
      rec.signal_value_numeric = last_battery_level_;
      rec.unit = "percentage";
      rec.observed_at = current_time_ms;
      rec.ingested_at = now_ms();
      rec.source = "system";
      rec.metadata =
          R"({"battery_id":"default","configured_by":"system","updated_at":)" +
          std::to_string(current_time_ms) + "}";
      emit_signal(rec);
      last_battery_emit_ = current_time_ms;
      battery_low_event_logged_ = true;
    }
  } else if (last_battery_level_ >= oro::health::BATTERY_LOW_THRESHOLD) {
    // Reset the low battery flag when battery level is healthy
    battery_low_event_logged_ = false;
    // Periodic snapshot emission with last known value
    bool time_elapsed = (current_time_ms - last_battery_emit_ >=
                         oro::health::BATTERY_SNAPSHOT_INTERVAL_MS);
    if (time_elapsed) {
      SignalRecord rec;
      rec.device_id = oro::health::DEVICE_ID;
      rec.signal_type = "battery_level";
      rec.signal_value_numeric = last_battery_level_;
      rec.unit = "percentage";
      rec.observed_at = current_time_ms;
      rec.ingested_at = now_ms();
      rec.source = "system";
      rec.metadata = R"({"event_time":)" + std::to_string(current_time_ms) +
                     R"(,"battery_id":"default","power_mode":"unknown"})";
      emit_signal(rec);
      last_battery_emit_ = current_time_ms;
    }
  }

  // ═══════════════════════════════════════════════════════════════════════════
  // #74 power_supply_status — periodic snapshot (10s)
  // ═══════════════════════════════════════════════════════════════════════════
  if (!last_power_status_.empty() &&
      (current_time_ms - last_power_emit_ms_ >=
       oro::health::POWER_SUPPLY_INTERVAL_MS)) {
    SignalRecord rec;
    rec.device_id = oro::health::DEVICE_ID;
    rec.signal_type = "power_supply_status";
    rec.signal_value_text = last_power_status_;
    rec.unit = "categorical";
    rec.observed_at = current_time_ms;
    rec.ingested_at = now_ms();
    rec.source = "system";
    rec.metadata = R"({"event_time":)" + std::to_string(current_time_ms) +
                   R"(,"source_type":")" + last_power_source_type_ +
                   R"(","voltage_state":")" + last_power_voltage_state_ + R"("})";

    emit_signal(rec);
    last_power_emit_ms_ = current_time_ms;
  }

  // ═══════════════════════════════════════════════════════════════════════════
  // #127 fountain_pump_health_status — Dry-run detection
  // ═══════════════════════════════════════════════════════════════════════════
  if (is_pump_running_ && !pump_damage_reported_) {
    if (current_time_ms - pump_on_start_time_ms_ >=
        oro::health::WATER_PUMP_HEALTH_TIMEOUT_MS) {
      if (last_bowl_level_ != 1) { // Not Full
        SignalRecord rec;
        rec.device_id = oro::health::DEVICE_ID;
        rec.signal_type = "fountain_pump_health_status";
        rec.signal_value_text = "failed";
        rec.unit = "categorical";
        rec.observed_at = current_time_ms;
        rec.ingested_at = now_ms();
        rec.source = "system";
        rec.metadata = R"({"pump_id":"pump_0","event_time":)" +
                       std::to_string(current_time_ms) +
                       R"(,"diagnostic_code":"PUMP_DRY_RUN_TIMEOUT"})";
        emit_signal(rec);
        pump_damage_reported_ = true;
      }
    }
  }

  // ═══════════════════════════════════════════════════════════════════════════
  // #76 frame_brightness_contrast_quality — periodic snapshot
  // ═══════════════════════════════════════════════════════════════════════════
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
      rec.metadata = R"({"event_time":)" + std::to_string(current_time_ms) +
                     R"(,"camera_id":"default","frame_id":""})";
      emit_signal(rec);
      last_frame_quality_emit_ = current_time_ms;
    }
  }

  // #106 last_seen_timestamp - [TBD] placeholder implementation
  /*
  if (current_time_ms - last_seen_emit_time_ >= 60000) {
      for (const auto& [entity, ts] : last_seen_timestamps_) {
          // emit_signal(...)
      }
      last_seen_emit_time_ = current_time_ms;
  }
  */

  // #107 communication_timeout_threshold - [TBD] placeholder implementation
  /*
  if (timeout_config_ > 0) {
      // emit_signal(...)
  }
  */
}

// ?????????????????????????????????????????????????????????????????????????????
// #71 ?? device_heartbeat (Periodic)
// ?????????????????????????????????????????????????????????????????????????????

void HealthMonitor::update_device_heartbeat(uint8_t seq_num,
                                            uint64_t current_time_ms) {
  // Track arrival from ZMQ (MCU heartbeat)
  last_hb_arrival_time_ms_ = current_time_ms;

  if (seq_num != last_hb_seq_) {
    last_hb_seq_ = seq_num;
    last_hb_change_time_ms_ = current_time_ms;
    if (hb_stale_) {
      std::cout << "[HealthMonitor] INFO: Hardware heartbeat recovered.\n";
      hb_stale_ = false;
    }
  }
}

// ?????????????????????????????????????????????????????????????????????????????
// #73 ?? battery_level (Hybrid: Periodic + Threshold Change)
// ?????????????????????????????????????????????????????????????????????????????

void HealthMonitor::update_battery_level(double level,
                                         const std::string &battery_id,
                                         const std::string &power_mode,
                                         uint64_t current_time_ms) {
  bool time_elapsed = (current_time_ms - last_battery_emit_ >=
                       oro::health::BATTERY_SNAPSHOT_INTERVAL_MS);
  bool significant_change =
      (last_battery_level_ < 0) || (std::abs(level - last_battery_level_) >=
                                    oro::health::BATTERY_CHANGE_THRESHOLD);

  if (time_elapsed || significant_change) {
    SignalRecord rec;
    rec.device_id = oro::health::DEVICE_ID;
    rec.signal_type = "battery_level";
    rec.signal_value_numeric = level;
    rec.unit = "percentage";
    rec.observed_at = current_time_ms;
    rec.ingested_at = now_ms();
    rec.source = "system";
    rec.metadata = R"({"event_time":)" + std::to_string(current_time_ms) +
                   R"(,"battery_id":")" + battery_id + R"(","power_mode":")" +
                   power_mode + R"("})";

    emit_signal(rec);
    last_battery_level_ = level;
    last_battery_emit_ = current_time_ms;
  }
}

// ?????????????????????????????????????????????????????????????????????????????
// #76 ?? frame_brightness_contrast_quality (Periodic)
// ?????????????????????????????????????????????????????????????????????????????

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
    rec.metadata = R"({"event_time":)" + std::to_string(current_time_ms) +
                   R"(,"camera_id":")" + camera_id + R"(","frame_id":")" +
                   frame_id + R"("})";

    emit_signal(rec);
    last_frame_quality_emit_ = current_time_ms;
  }
}

// ?????????????????????????????????????????????????????????????????????????????
// #72 ?? device_connectivity_status (Change-based)
// ?????????????????????????????????????????????????????????????????????????????

void HealthMonitor::update_device_connectivity_status(
    const std::string &status, const std::string & /*network_type*/,
    const std::string & /*signal_strength*/, uint64_t current_time_ms) {
  if (status != last_connectivity_status_) {
    NetworkInfo sys_info = get_system_network_info();

    SignalRecord rec;
    rec.device_id = oro::health::DEVICE_ID;
    rec.signal_type = "device_connectivity_status";
    rec.signal_value_text = status;
    rec.unit = "categorical";
    rec.observed_at = current_time_ms;
    rec.ingested_at = now_ms();
    rec.source = "system";
    rec.metadata = R"({"event_time":)" + std::to_string(current_time_ms) +
                   R"(,"network_type":")" + sys_info.type +
                   R"(","signal_strength":")" + sys_info.strength + R"("})";

    emit_signal(rec);
    last_connectivity_status_ = status;
  }
}

HealthMonitor::NetworkInfo HealthMonitor::get_system_network_info() {
  NetworkInfo info;
  info.type = "unknown";
  info.strength = "N/A";

  namespace fs = std::filesystem;
  const std::string sys_net = "/sys/class/net/";

  try {
    if (fs::exists(sys_net)) {
      for (const auto &entry : fs::directory_iterator(sys_net)) {
        std::string iface = entry.path().filename().string();
        if (iface == "lo")
          continue;

        std::ifstream operstate_file(sys_net + iface + "/operstate");
        std::string state;
        if (operstate_file >> state && state == "up") {
          if (iface.rfind("wl", 0) == 0) {
            info.type = "wifi";
            // Get wifi signal strength from /proc/net/wireless
            std::ifstream wireless("/proc/net/wireless");
            std::string line;
            while (std::getline(wireless, line)) {
              if (line.find(iface) != std::string::npos) {
                std::stringstream ss(line);
                std::string dummy;
                ss >> dummy >> dummy >> dummy >> info.strength;
                if (!info.strength.empty() && info.strength.back() == '.') {
                  info.strength.pop_back();
                }
                info.strength += " dBm";
                break;
              }
            }
          } else if (iface.rfind("eth", 0) == 0 || iface.rfind("en", 0) == 0) {
            info.type = "ethernet";
          }
          break; // Found an active interface
        }
      }
    }
  } catch (...) {
  }

  return info;
}

// ?????????????????????????????????????????????????????????????????????????????
// #74 ?? power_supply_status (Change-based)
// ?????????????????????????????????????????????????????????????????????????????

void HealthMonitor::update_power_supply_status(const std::string &status,
                                               const std::string &source_type,
                                               const std::string &voltage_state,
                                               uint64_t /*current_time_ms*/) {
  // Cache values for periodic emission in tick()
  last_power_status_ = status;
  last_power_source_type_ = source_type;
  last_power_voltage_state_ = voltage_state;
}

// ?????????????????????????????????????????????????????????????????????????????
// #75 ?? camera_obstruction_status (Change-based)
// ?????????????????????????????????????????????????????????????????????????????

void HealthMonitor::update_camera_obstruction_status(
    bool obstructed, const std::string &camera_id, double confidence_score,
    uint64_t current_time_ms) {
  if (!has_last_camera_obstruction_ || last_camera_obstruction_ != obstructed) {
    SignalRecord rec;
    rec.device_id = oro::health::DEVICE_ID;
    rec.signal_type = "camera_obstruction_status";
    rec.signal_value_boolean = obstructed;
    rec.unit = "boolean";
    rec.observed_at = current_time_ms;
    rec.ingested_at = now_ms();
    rec.source = "system";
    rec.confidence = confidence_score;
    rec.metadata = R"({"event_time":)" + std::to_string(current_time_ms) +
                   R"(,"camera_id":")" + camera_id +
                   R"(","confidence_score":)" +
                   std::to_string(confidence_score) + "}";

    emit_signal(rec);
    last_camera_obstruction_ = obstructed;
    has_last_camera_obstruction_ = true;
  }
}

// ?????????????????????????????????????????????????????????????????????????????
// #77 ?? sensor_health_status (Change-based, per sensor)
// ?????????????????????????????????????????????????????????????????????????????

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
    rec.metadata = R"({"event_time":)" + std::to_string(current_time_ms) +
                   R"(,"sensor_id":")" + sensor_id +
                   R"(","diagnostic_code":")" + diagnostic_code + R"("})";

    emit_signal(rec);
    last_sensor_health_[sensor_id] = status;
  }
}

// ?????????????????????????????????????????????????????????????????????????????
// #78 ?? sensor_communication_status (Change-based, per sensor)
// ?????????????????????????????????????????????????????????????????????????????

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
    rec.metadata = R"({"event_time":)" + std::to_string(current_time_ms) +
                   R"(,"sensor_id":")" + sensor_id + R"(","bus_interface":")" +
                   bus_interface + R"("})";

    emit_signal(rec);
    last_sensor_comm_[sensor_id] = status;
  }
}

// ?????????????????????????????????????????????????????????????????????????????
// #79 ?? firmware_version (Config-change, per component)
// ?????????????????????????????????????????????????????????????????????????????

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
                   R"(","event_time":)" + std::to_string(current_time_ms) +
                   R"(,"build_id":")" + build_id + R"("})";

    emit_signal(rec);
    firmware_versions_[component_name] = version;
  }
}

// ?????????????????????????????????????????????????????????????????????????????
// #80 ?? latest_firmware_version_available (Config-change, per component)
// ?????????????????????????????????????????????????????????????????????????????

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
                   R"(","checked_at":)" + std::to_string(current_time_ms) +
                   R"(,"source":")" + source + R"("})";

    emit_signal(rec);
    latest_firmware_available_[component_name] = version;
  }
}

// ?????????????????????????????????????????????????????????????????????????????
// #81 ?? firmware_update_availability_flag (Change-based, per component)
// ?????????????????????????????????????????????????????????????????????????????

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
    rec.metadata = R"({"checked_at":)" + std::to_string(current_time_ms) +
                   R"(,"component_name":")" + component_name +
                   R"(","current_version":")" + current_version + R"("})";

    emit_signal(rec);
    firmware_update_flags_[component_name] = available;
  }
}

// ?????????????????????????????????????????????????????????????????????????????
// #82 ?? firmware_update_completion_event (Event ?? emit once)
// ?????????????????????????????????????????????????????????????????????????????

void HealthMonitor::emit_firmware_update_complete(
    const std::string &component_name, const std::string &target_version,
    uint64_t current_time_ms) {
  // Events always emit ?? no state-change gating.
  SignalRecord rec;
  rec.device_id = oro::health::DEVICE_ID;
  rec.signal_type = "firmware_update_completion_event";
  rec.signal_value_text = "completed";
  rec.unit = "event";
  rec.observed_at = current_time_ms;
  rec.ingested_at = now_ms();
  rec.source = "system";
  rec.metadata = R"({"component_name":")" + component_name +
                 R"(","target_version":")" + target_version +
                 R"(","event_time":)" + std::to_string(current_time_ms) + "}";
  emit_signal(rec);
}

// ============================================================================
// #107 ?? communication_timeout_threshold (Config-change)
// ============================================================================

void HealthMonitor::update_water_pump_state(const std::string &status,
                                            uint64_t current_time_ms) {
  // Detection of start/stop events
  if (status != last_pump_state_) {
    // #128 water_fountain_status - Emit on start/stop
    SignalRecord rec128;
    rec128.device_id = oro::health::DEVICE_ID;
    rec128.signal_type = "water_fountain_status";
    rec128.signal_value_text = status; // "running" or "idle"
    rec128.unit = "categorical";
    rec128.observed_at = current_time_ms;
    rec128.ingested_at = now_ms();
    rec128.source = "system";
    rec128.metadata = R"({"fountain_id":"fountain_0","event_time":)" +
                      std::to_string(current_time_ms) +
                      R"(,"operating_mode":")" +
                      (status == "running" ? "active" : "idle") + R"("})";
    emit_signal(rec128);

    // Transition Logic
    if (status == "running") {
      is_pump_running_ = true;
      pump_on_start_time_ms_ = current_time_ms;
      pump_damage_reported_ = false;
    } else {
      is_pump_running_ = false;

      // #127 fountain_pump_health_status - Also emit healthy status when
      // stopping if it wasn't reported as damaged.
      if (!pump_damage_reported_) {
        SignalRecord rec127;
        rec127.device_id = oro::health::DEVICE_ID;
        rec127.signal_type = "fountain_pump_health_status";
        rec127.signal_value_text = "healthy";
        rec127.unit = "categorical";
        rec127.observed_at = current_time_ms;
        rec127.ingested_at = now_ms();
        rec127.source = "system";
        rec127.metadata = R"({"pump_id":"pump_0","event_time":)" +
                          std::to_string(current_time_ms) +
                          R"(,"diagnostic_code":"OK"})";
        emit_signal(rec127);
      }
    }

    last_pump_state_ = status;
  }
}

void HealthMonitor::update_bowl_water_level(int state,
                                            uint64_t current_time_ms) {
  last_bowl_level_ = state;

  // If bowl becomes full (1) while pump is running, we can treat it as a
  // success signal though the primary failure detection is in tick().
  if (state == 1 && is_pump_running_) {
    // Optional: could log a healthy signal here too if needed.
  }
}

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
                   R"(","updated_at":)" + std::to_string(current_time_ms) + "}";

    emit_signal(rec);
    timeout_config_ = threshold;
  }
}

// ?????????????????????????????????????????????????????????????????????????????
// #110 ?? battery_low_threshold (Config-change)
// ?????????????????????????????????????????????????????????????????????????????

void HealthMonitor::update_battery_low_config(double threshold,
                                              const std::string &battery_id,
                                              const std::string &configured_by,
                                              uint64_t current_time_ms) {
  if (std::abs(threshold - battery_low_config_) > 0.0001) {
    SignalRecord rec;
    rec.device_id = oro::health::DEVICE_ID;
    rec.signal_type = "battery_low_threshold";
    rec.signal_value_numeric = threshold;
    rec.unit = "percentage";
    rec.observed_at = current_time_ms;
    rec.ingested_at = now_ms();
    rec.source = "system";
    rec.metadata = R"({"battery_id":")" + battery_id +
                   R"(","configured_by":")" + configured_by +
                   R"(","updated_at":)" + std::to_string(current_time_ms) + "}";

    emit_signal(rec);
    battery_low_config_ = threshold;
  }
}

// ?????????????????????????????????????????????????????????????????????????????
// #127 ?? fountain_pump_health_status (Change-based, per pump)
// ?????????????????????????????????????????????????????????????????????????????

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
    rec.metadata = R"({"pump_id":")" + pump_id + R"(","event_time":)" +
                   std::to_string(current_time_ms) + R"(,"diagnostic_code":")" +
                   diagnostic_code + R"("})";

    emit_signal(rec);
    fountain_pump_health_[pump_id] = status;
  }
}

// ?????????????????????????????????????????????????????????????????????????????
// #128 ?? water_fountain_status (Change-based, per fountain)
// ?????????????????????????????????????????????????????????????????????????????

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
    rec.metadata = R"({"fountain_id":")" + fountain_id + R"(","event_time":)" +
                   std::to_string(current_time_ms) + R"(,"operating_mode":")" +
                   operating_mode + R"("})";

    emit_signal(rec);
    water_fountain_status_[fountain_id] = status;
  }
}

// ?????????????????????????????????????????????????????????????????????????????
// #98 ?? settings_apply_success_status (TODO: needs ownership clarity)
// ?????????????????????????????????????????????????????????????????????????????

void HealthMonitor::update_settings_apply_status(
    const std::string &settings_profile_id, bool success,
    const std::string &failure_reason, uint64_t current_time_ms) {
  SignalRecord rec;
  rec.device_id = oro::health::DEVICE_ID;
  rec.signal_type = "settings_apply_success_status";
  rec.signal_value_boolean = success;
  rec.unit = "boolean";
  rec.observed_at = current_time_ms;
  rec.ingested_at = now_ms();
  rec.source = "system";
  rec.metadata = R"({"settings_profile_id":")" + settings_profile_id +
                 R"(","applied_at":)" + std::to_string(current_time_ms) +
                 R"(,"failure_reason":")" + failure_reason + R"("})";

  emit_signal(rec);
}

// ?????????????????????????????????????????????????????????????????????????????
// #106 ?? last_seen_timestamp (TODO: needs discussion on emission strategy)
// ?????????????????????????????????????????????????????????????????????????????

void HealthMonitor::update_last_seen(const std::string &entity_type,
                                     const std::string &entity_id,
                                     uint64_t current_time_ms) {
  std::string key = entity_type + ":" + entity_id;
  last_seen_timestamps_[key] = current_time_ms;

  SignalRecord rec;
  rec.device_id = oro::health::DEVICE_ID;
  rec.signal_type = "last_seen_timestamp";
  rec.signal_value_text =
      storage_handoff::StorageWriter::unix_ms_to_iso8601(current_time_ms);
  rec.unit = "timestamp";
  rec.observed_at = current_time_ms;
  rec.ingested_at = now_ms();
  rec.source = "system";
  rec.metadata = R"({"entity_type":")" + entity_type + R"(","entity_id":")" +
                 entity_id + R"(","source":"system"})";

  emit_signal(rec);
}
