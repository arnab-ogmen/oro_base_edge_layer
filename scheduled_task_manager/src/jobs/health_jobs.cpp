#include "scheduled_task_manager/jobs/health_jobs.hpp"
#include <iostream>

namespace oro::stm::jobs {

void prepare_health_job_statements(storage_handoff::StorageWriter &writer) {
  // Daily food consumption total (today)
  writer.prepare("stm_health_food_today",
    R"(SELECT COALESCE(SUM(signal_value_numeric), 0)
       FROM oro_base_signals
       WHERE device_id = $1::uuid AND signal_type = 'food_consumption_grams'
         AND observed_at >= CURRENT_DATE)");

  // Daily water consumption total (today)
  writer.prepare("stm_health_water_today",
    R"(SELECT COALESCE(SUM(signal_value_numeric), 0)
       FROM oro_base_signals
       WHERE device_id = $1::uuid AND signal_type = 'water_consumption_ml'
         AND observed_at >= CURRENT_DATE)");

  // Food baseline (7-day average)
  writer.prepare("stm_health_food_baseline",
    R"(SELECT COALESCE(AVG(daily_total), 0) FROM (
         SELECT DATE(observed_at) AS d, SUM(signal_value_numeric) AS daily_total
         FROM oro_base_signals
         WHERE device_id = $1::uuid AND signal_type = 'food_consumption_grams'
           AND observed_at >= CURRENT_DATE - INTERVAL '7 days'
           AND observed_at < CURRENT_DATE
         GROUP BY DATE(observed_at)
       ) AS dt)");

  // Water baseline (7-day average)
  writer.prepare("stm_health_water_baseline",
    R"(SELECT COALESCE(AVG(daily_total), 0) FROM (
         SELECT DATE(observed_at) AS d, SUM(signal_value_numeric) AS daily_total
         FROM oro_base_signals
         WHERE device_id = $1::uuid AND signal_type = 'water_consumption_ml'
           AND observed_at >= CURRENT_DATE - INTERVAL '7 days'
           AND observed_at < CURRENT_DATE
         GROUP BY DATE(observed_at)
       ) AS dt)");

  // Average temperature (last 30 min)
  writer.prepare("stm_health_avg_temp",
    R"(SELECT COALESCE(AVG(signal_value_numeric), 0)
       FROM oro_base_signals
       WHERE device_id = $1::uuid AND signal_type = 'ambient_temperature'
         AND observed_at >= NOW() - INTERVAL '30 minutes')");

  // Insert a health alert event
  writer.prepare("stm_health_emit_event",
    R"(INSERT INTO oro_base_events
         (device_id, dog_id, event_type, category, event_source, severity,
          status, trigger_mode, detected_at, event_start_at,
          title, description, payload, dedupe_key, notification_eligible,
          created_at, updated_at)
       VALUES
         ($1::uuid, $2, $3, 'Health', 'scheduled_task_manager', $4,
          'open', 'scheduled', NOW(), NOW(),
          $5, $6, $7::jsonb, $8, true, NOW(), NOW()))");

  // Insert baseline signal
  writer.prepare("stm_health_write_baseline",
    R"(INSERT INTO oro_base_signals
         (device_id, signal_type, signal_value_numeric, unit,
          observed_at, ingested_at, source, created_at)
       VALUES
         ($1::uuid, $2, $3, $4, NOW(), NOW(), 'scheduled_task_manager', NOW()))");
}

JobResult health_signal_evaluator(const nlohmann::json &config,
                                  storage_handoff::StorageWriter &writer) {
  std::cout << "[HealthJobs] health_signal_evaluator executing...\n";
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

  // 1. Evaluate food consumption vs baseline
  double food_today = writer.query_double("stm_health_food_today", device_id);
  double food_baseline =
      writer.query_double("stm_health_food_baseline", device_id);

  if (food_baseline > 0.0) {
    double ratio = food_today / food_baseline;
    result.metadata["food_today_grams"] = food_today;
    result.metadata["food_baseline_grams"] = food_baseline;
    result.metadata["food_ratio"] = ratio;

    if (ratio < 0.25 && food_today > 0.0) {
      std::cout << "[HealthJobs] Abnormally low eating detected.\n";
      nlohmann::json payload = {{"food_today", food_today},
                                {"baseline", food_baseline},
                                {"ratio", ratio}};
      writer.execute_prepared("stm_health_emit_event",
          device_id, std::optional<std::string>{},
          std::string("abnormal_low_eating"), std::string("high"),
          std::string("Low Food Intake"),
          std::string("Food consumption is significantly below baseline."),
          payload.dump(),
          "STM_LOW_EAT_" + device_id);

      writer.execute_prepared("stm_emit_notification",
          device_id, std::optional<std::string>{},
          std::string("insight"), std::string("Health"),
          "low_eating_" + device_id,
          std::string("Low Food Intake Alert"),
          std::string("Food consumption is significantly below baseline."),
          std::string("high"),
          payload.dump(),
          "STM_NOTIF_LOW_EAT_" + device_id);

      result.items_processed++;
    } else if (ratio > 2.0) {
      std::cout << "[HealthJobs] Abnormally high eating detected.\n";
      nlohmann::json payload = {{"food_today", food_today},
                                {"baseline", food_baseline},
                                {"ratio", ratio}};
      writer.execute_prepared("stm_health_emit_event",
          device_id, std::optional<std::string>{},
          std::string("abnormal_high_eating"), std::string("medium"),
          std::string("High Food Intake"),
          std::string("Food consumption is significantly above baseline."),
          payload.dump(),
          "STM_HIGH_EAT_" + device_id);

      writer.execute_prepared("stm_emit_notification",
          device_id, std::optional<std::string>{},
          std::string("insight"), std::string("Health"),
          "high_eating_" + device_id,
          std::string("High Food Intake Alert"),
          std::string("Food consumption is significantly above baseline."),
          std::string("medium"),
          payload.dump(),
          "STM_NOTIF_HIGH_EAT_" + device_id);

      result.items_processed++;
    }
  }

  // 2. Evaluate water consumption vs baseline
  double water_today = writer.query_double("stm_health_water_today", device_id);
  double water_baseline =
      writer.query_double("stm_health_water_baseline", device_id);

  if (water_baseline > 0.0) {
    double ratio = water_today / water_baseline;
    result.metadata["water_today_ml"] = water_today;
    result.metadata["water_baseline_ml"] = water_baseline;

    if (ratio < 0.25 && water_today > 0.0) {
      nlohmann::json payload = {{"water_today", water_today},
                                {"baseline", water_baseline}};
      writer.execute_prepared("stm_health_emit_event",
          device_id, std::optional<std::string>{},
          std::string("abnormal_low_drinking"), std::string("high"),
          std::string("Low Water Intake"),
          std::string("Water consumption is significantly below baseline."),
          payload.dump(),
          "STM_LOW_WATER_" + device_id);

      writer.execute_prepared("stm_emit_notification",
          device_id, std::optional<std::string>{},
          std::string("insight"), std::string("Health"),
          "low_water_" + device_id,
          std::string("Low Water Intake Alert"),
          std::string("Water consumption is significantly below baseline."),
          std::string("high"),
          payload.dump(),
          "STM_NOTIF_LOW_WATER_" + device_id);

      result.items_processed++;
    }
  }

  // 3. Environmental comfort check
  double avg_temp = writer.query_double("stm_health_avg_temp", device_id);
  if (avg_temp > 0.0) {
    double min_temp = 16.0, max_temp = 30.0;
    if (config.contains("environment_condition_node") &&
        config["environment_condition_node"].contains("comfort_thresholds")) {
      auto &ct = config["environment_condition_node"]["comfort_thresholds"];
      min_temp = ct.value("/environment_comfort_range/min"_json_pointer, 16.0);
      max_temp = ct.value("/environment_comfort_range/max"_json_pointer, 30.0);
    }
    result.metadata["avg_temperature"] = avg_temp;

    if (avg_temp < min_temp || avg_temp > max_temp) {
      nlohmann::json payload = {{"avg_temp", avg_temp},
                                {"comfort_min", min_temp},
                                {"comfort_max", max_temp}};
      writer.execute_prepared("stm_health_emit_event",
          device_id, std::optional<std::string>{},
          std::string("environment_discomfort"), std::string("medium"),
          std::string("Temperature Out of Comfort Range"),
          std::string("Room temperature is outside the configured comfort range."),
          payload.dump(),
          "STM_TEMP_" + device_id);

      writer.execute_prepared("stm_emit_notification",
          device_id, std::optional<std::string>{},
          std::string("insight"), std::string("Health"),
          "temp_discomfort_" + device_id,
          std::string("Temperature Discomfort Alert"),
          std::string("Room temperature is outside the configured comfort range."),
          std::string("medium"),
          payload.dump(),
          "STM_NOTIF_TEMP_" + device_id);

      result.items_processed++;
    }
  }

  std::cout << "[HealthJobs] health_signal_evaluator done. "
            << result.items_processed << " insight(s).\n";
  return result;
}

JobResult baseline_recalculation(const nlohmann::json &config,
                                 storage_handoff::StorageWriter &writer) {
  std::cout << "[HealthJobs] baseline_recalculation executing...\n";
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

  // Recalculate and store baselines
  double food_bl = writer.query_double("stm_health_food_baseline", device_id);
  if (food_bl > 0.0) {
    writer.execute_prepared("stm_health_write_baseline",
        device_id, std::string("pet_baseline_food_daily"),
        food_bl, std::string("grams"));
    result.items_processed++;
    std::cout << "[HealthJobs] Food baseline: " << food_bl << "g/day\n";
  }

  double water_bl = writer.query_double("stm_health_water_baseline", device_id);
  if (water_bl > 0.0) {
    writer.execute_prepared("stm_health_write_baseline",
        device_id, std::string("pet_baseline_water_daily"),
        water_bl, std::string("ml"));
    result.items_processed++;
    std::cout << "[HealthJobs] Water baseline: " << water_bl << "ml/day\n";
  }

  std::cout << "[HealthJobs] baseline_recalculation done. "
            << result.items_processed << " baseline(s) updated.\n";
  return result;
}

} // namespace oro::stm::jobs
