#include "scheduled_task_manager/jobs/device_jobs.hpp"
#include <chrono>
#include <iostream>

namespace oro::stm::jobs {

void prepare_device_job_statements(storage_handoff::StorageWriter &writer) {
  // Check device last_seen_at staleness in minutes (legacy/fallback)
  writer.prepare("stm_device_staleness_minutes",
    R"(SELECT EXTRACT(EPOCH FROM (NOW() - last_seen_at)) / 60.0
       FROM oro_base_device WHERE device_id = $1::uuid)");

  // Get device status
  writer.prepare("stm_device_status",
    R"(SELECT status FROM oro_base_device WHERE device_id = $1::uuid)");

  // Get latest battery signal value
  writer.prepare("stm_device_battery_level",
    R"(SELECT signal_value_numeric FROM oro_base_signals
       WHERE device_id = $1::uuid AND signal_type = 'battery_level'
       ORDER BY observed_at DESC LIMIT 1)");

  // Check sensor freshness: minutes since last signal of a given type
  writer.prepare("stm_sensor_freshness_minutes",
    R"(SELECT COALESCE(
         EXTRACT(EPOCH FROM (NOW() - MAX(observed_at))) / 60.0,
         9999.0
       ) FROM oro_base_signals
       WHERE device_id = $1::uuid AND signal_type = $2)");

  // Get latest communication timeout threshold
  writer.prepare("stm_communication_timeout_threshold",
    R"(SELECT COALESCE(
         (SELECT signal_value_numeric FROM oro_base_signals
          WHERE device_id = $1::uuid AND signal_type = 'communication_timeout_threshold'
          ORDER BY observed_at DESC LIMIT 1),
         15.0
       ))");

  // Get latest battery low threshold
  writer.prepare("stm_battery_low_threshold",
    R"(SELECT COALESCE(
         (SELECT signal_value_numeric FROM oro_base_signals
          WHERE device_id = $1::uuid AND signal_type = 'battery_low_threshold'
          ORDER BY observed_at DESC LIMIT 1),
         20.0
       ))");

  // Get latest water refill required status
  writer.prepare("stm_device_water_supply_refill",
    R"(SELECT COALESCE(
         (SELECT signal_value_boolean::int FROM oro_base_signals
          WHERE device_id = $1::uuid AND signal_type = 'water_refill_required'
          ORDER BY observed_at DESC LIMIT 1),
         0
       ))");

  // Emit device alert event
  writer.prepare("stm_device_emit_event",
    R"(INSERT INTO oro_base_events
         (device_id, dog_id, event_type, category, event_source, severity,
          status, trigger_mode, detected_at, event_start_at,
          title, description, payload, dedupe_key, notification_eligible,
          created_at, updated_at)
       VALUES
         ($1::uuid, NULL, $2, 'Device Health', 'scheduled_task_manager', $3,
          'open', 'scheduled', NOW(), NOW(),
          $4, $5, $6::jsonb, $7, true, NOW(), NOW()))");
}

JobResult device_health_check(const nlohmann::json &config,
                              storage_handoff::StorageWriter &writer) {
  std::cout << "[DeviceJobs] device_health_check executing...\n";
  JobResult result;
  result.success = true;
  result.items_processed = 0;

  std::string device_id =
      config.value("/global/device_id"_json_pointer, std::string(""));
  if (device_id.empty()) {
    result.error = "No device_id";
    result.success = false;
    return result;
  }

  // 1. Check device staleness (heartbeat) using signals table
  double timeout_threshold =
      writer.query_double("stm_communication_timeout_threshold", device_id);
  double heartbeat_stale_mins =
      writer.query_double("stm_sensor_freshness_minutes", device_id, std::string("device_heartbeat"));

  if (heartbeat_stale_mins > timeout_threshold) {
    std::cout << "[DeviceJobs] Device offline: last heartbeat seen " << heartbeat_stale_mins
              << " minutes ago (threshold: " << timeout_threshold << " mins).\n";
    std::string severity = (heartbeat_stale_mins > timeout_threshold * 2.0) ? "high" : "medium";
    nlohmann::json payload = {
      {"minutes_since_seen", heartbeat_stale_mins},
      {"timeout_threshold", timeout_threshold}
    };
    writer.execute_prepared("stm_device_emit_event",
        device_id, std::string("device_offline"), severity,
        std::string("Device Offline"),
        std::string("Device has not sent a heartbeat recently."),
        payload.dump(),
        "STM_OFFLINE_" + device_id);

    writer.execute_prepared("stm_emit_notification",
        device_id,
        std::optional<std::string>{},
        std::string("system"),
        std::string("Device Health"),
        "device_offline_" + device_id,
        std::string("Device Offline"),
        "Device has not sent a heartbeat in " + std::to_string(static_cast<int>(heartbeat_stale_mins)) + " minutes.",
        severity,
        payload.dump(),
        "STM_NOTIF_OFFLINE_" + device_id);

    result.items_processed++;
    result.metadata["offline_minutes"] = heartbeat_stale_mins;
  }

  // 2. Check battery level using signals table
  double battery_low_threshold =
      writer.query_double("stm_battery_low_threshold", device_id);
  double battery = writer.query_double("stm_device_battery_level", device_id);

  if (battery > 0.0 && battery < battery_low_threshold) {
    std::string severity = (battery < 5.0) ? "critical" : "medium";
    std::string event_type =
        (battery < 5.0) ? "critical_battery" : "low_battery";
    nlohmann::json payload = {
      {"battery_percent", battery},
      {"battery_low_threshold", battery_low_threshold}
    };
    writer.execute_prepared("stm_device_emit_event",
        device_id, event_type, severity,
        std::string("Low Battery"),
        std::string("Device battery level is low."),
        payload.dump(),
        "STM_BATTERY_" + device_id);

    writer.execute_prepared("stm_emit_notification",
        device_id,
        std::optional<std::string>{},
        std::string("system"),
        std::string("Device Health"),
        "device_battery_" + device_id,
        std::string("Low Battery Alert"),
        "Device battery level is low (" + std::to_string(static_cast<int>(battery)) + "%).",
        severity,
        payload.dump(),
        "STM_NOTIF_BATTERY_" + device_id);

    result.items_processed++;
    result.metadata["battery_percent"] = battery;
    std::cout << "[DeviceJobs] Low battery: " << battery << "%\n";
  }

  // 3. Check low supply (water_refill_required)
  int water_refill = writer.query_int("stm_device_water_supply_refill", device_id);
  if (water_refill == 1) {
    nlohmann::json payload = {{"water_refill_required", true}};
    writer.execute_prepared("stm_device_emit_event",
        device_id, std::string("low_supply"), std::string("medium"),
        std::string("Low Supply Warning"),
        std::string("Water tank refill is required."),
        payload.dump(),
        "STM_LOW_SUPPLY_" + device_id);

    writer.execute_prepared("stm_emit_notification",
        device_id,
        std::optional<std::string>{},
        std::string("system"),
        std::string("Device Health"),
        "device_low_supply_" + device_id,
        std::string("Low Supply Alert"),
        "Water tank refill is required.",
        std::string("medium"),
        payload.dump(),
        "STM_NOTIF_LOW_SUPPLY_" + device_id);

    result.items_processed++;
    std::cout << "[DeviceJobs] Low Supply: Water refill required.\n";
  }

  // 4. Check device status
  std::string status = writer.query_string("stm_device_status", device_id);
  if (status == "error") {
    nlohmann::json payload = {{"device_status", status}};
    writer.execute_prepared("stm_device_emit_event",
        device_id, std::string("device_error_state"), std::string("high"),
        std::string("Device Error"),
        std::string("Device is in error state."),
        payload.dump(),
        "STM_ERROR_" + device_id);

    writer.execute_prepared("stm_emit_notification",
        device_id,
        std::optional<std::string>{},
        std::string("system"),
        std::string("Device Health"),
        "device_error_" + device_id,
        std::string("Device Error"),
        std::string("Device is in error state."),
        std::string("high"),
        payload.dump(),
        "STM_NOTIF_ERROR_" + device_id);

    result.items_processed++;
  }

  // 5. Stale sensor data check
  struct SensorCheck {
    const char *signal_type;
    const char *display;
    double max_age_minutes;
  };

  SensorCheck checks[] = {
      {"food_bowl_weight", "Food Bowl Weight", 30.0},
      {"water_bowl_level", "Water Bowl Level", 30.0},
      {"ambient_temperature", "Ambient Temperature", 15.0},
      {"environment_temperature", "Environment Temperature", 15.0},
      {"ambient_humidity", "Ambient Humidity", 15.0},
      {"ambient_light_level", "Ambient Light Level", 15.0},
  };

  for (const auto &check : checks) {
    double age_mins = writer.query_double("stm_sensor_freshness_minutes",
                                          device_id,
                                          std::string(check.signal_type));
    if (age_mins > check.max_age_minutes) {
      std::cout << "[DeviceJobs] Stale sensor: " << check.display << " ("
                << age_mins << " min, max " << check.max_age_minutes << ")\n";

      nlohmann::json payload = {{"signal_type", check.signal_type},
                                {"age_minutes", age_mins},
                                {"max_age_minutes", check.max_age_minutes}};
      writer.execute_prepared("stm_device_emit_event",
          device_id, std::string("sensor_data_stale"), std::string("medium"),
          std::string(std::string("Stale: ") + check.display),
          std::string("No recent data from this sensor."),
          payload.dump(),
          std::string("STM_STALE_") + check.signal_type + "_" + device_id);

      writer.execute_prepared("stm_emit_notification",
          device_id,
          std::optional<std::string>{},
          std::string("system"),
          std::string("Device Health"),
          std::string("sensor_stale_") + check.signal_type + "_" + device_id,
          std::string(std::string("Stale Sensor: ") + check.display),
          std::string("No recent data received from ") + check.display + ".",
          std::string("medium"),
          payload.dump(),
          std::string("STM_NOTIF_STALE_") + check.signal_type + "_" + device_id);

      result.items_processed++;
    }
  }

  std::cout << "[DeviceJobs] device_health_check done. "
            << result.items_processed << " issue(s).\n";
  return result;
}

JobResult sensor_data_freshness_check(const nlohmann::json &config,
                                      storage_handoff::StorageWriter &writer) {
  std::cout << "[DeviceJobs] sensor_data_freshness_check executing...\n";
  JobResult result;
  result.success = true;
  result.items_processed = 0;

  std::string device_id =
      config.value("/global/device_id"_json_pointer, std::string(""));
  if (device_id.empty()) {
    result.error = "No device_id";
    result.success = false;
    return result;
  }

  // Sensor types and their expected max age in minutes
  struct SensorCheck {
    const char *signal_type;
    const char *display;
    double max_age_minutes;
  };

  SensorCheck checks[] = {
      {"food_bowl_weight", "Food Bowl Weight", 30.0},
      {"water_bowl_level", "Water Bowl Level", 30.0},
      {"ambient_temperature", "Ambient Temperature", 15.0},
      {"environment_temperature", "Environment Temperature", 15.0},
      {"ambient_humidity", "Ambient Humidity", 15.0},
      {"ambient_light_level", "Ambient Light Level", 15.0},
  };

  for (const auto &check : checks) {
    double age_mins = writer.query_double("stm_sensor_freshness_minutes",
                                          device_id,
                                          std::string(check.signal_type));
    if (age_mins > check.max_age_minutes) {
      std::cout << "[DeviceJobs] Stale sensor: " << check.display << " ("
                << age_mins << " min, max " << check.max_age_minutes << ")\n";

      nlohmann::json payload = {{"signal_type", check.signal_type},
                                {"age_minutes", age_mins},
                                {"max_age_minutes", check.max_age_minutes}};
      writer.execute_prepared("stm_device_emit_event",
          device_id, std::string("sensor_data_stale"), std::string("medium"),
          std::string(std::string("Stale: ") + check.display),
          std::string("No recent data from this sensor."),
          payload.dump(),
          std::string("STM_STALE_") + check.signal_type + "_" + device_id);

      writer.execute_prepared("stm_emit_notification",
          device_id,
          std::optional<std::string>{},
          std::string("system"),
          std::string("Device Health"),
          std::string("sensor_stale_") + check.signal_type + "_" + device_id,
          std::string(std::string("Stale Sensor: ") + check.display),
          std::string("No recent data received from ") + check.display + ".",
          std::string("medium"),
          payload.dump(),
          std::string("STM_NOTIF_STALE_") + check.signal_type + "_" + device_id);

      result.items_processed++;
    }
  }

  std::cout << "[DeviceJobs] sensor_data_freshness_check done. "
            << result.items_processed << " stale sensor(s).\n";
  return result;
}

} // namespace oro::stm::jobs
