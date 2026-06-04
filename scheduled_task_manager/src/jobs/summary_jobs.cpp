#include "scheduled_task_manager/jobs/summary_jobs.hpp"
#include <iostream>

namespace oro::stm::jobs {

void prepare_summary_job_statements(storage_handoff::StorageWriter &writer) {
  // Daily food total
  writer.prepare("stm_summary_food_daily",
    R"(SELECT COALESCE(SUM(signal_value_numeric), 0)
       FROM oro_base_signals
       WHERE device_id = $1::uuid AND signal_type = 'food_consumption_grams'
         AND observed_at >= CURRENT_DATE AND observed_at < CURRENT_DATE + 1)");

  // Daily water total
  writer.prepare("stm_summary_water_daily",
    R"(SELECT COALESCE(SUM(signal_value_numeric), 0)
       FROM oro_base_signals
       WHERE device_id = $1::uuid AND signal_type = 'water_consumption_ml'
         AND observed_at >= CURRENT_DATE AND observed_at < CURRENT_DATE + 1)");

  // Daily avg temperature
  writer.prepare("stm_summary_avg_temp_daily",
    R"(SELECT COALESCE(AVG(signal_value_numeric), 0)
       FROM oro_base_signals
       WHERE device_id = $1::uuid AND signal_type = 'ambient_temperature'
         AND observed_at >= CURRENT_DATE AND observed_at < CURRENT_DATE + 1)");

  // Daily avg humidity
  writer.prepare("stm_summary_avg_humidity_daily",
    R"(SELECT COALESCE(AVG(signal_value_numeric), 0)
       FROM oro_base_signals
       WHERE device_id = $1::uuid AND signal_type = 'ambient_humidity'
         AND observed_at >= CURRENT_DATE AND observed_at < CURRENT_DATE + 1)");

  // Daily event count
  writer.prepare("stm_summary_event_count_daily",
    R"(SELECT COUNT(*) FROM oro_base_events
       WHERE device_id = $1::uuid AND detected_at >= CURRENT_DATE
         AND detected_at < CURRENT_DATE + 1)");

  // Insert daily/weekly summary
  writer.prepare("stm_summary_insert",
    R"(INSERT INTO oro_base_summary
         (device_id, dog_id, summary_type, period_start, period_end,
          summary_date, status, title, payload, generated_at,
          generation_version, source_signal_window, created_at, updated_at)
       VALUES
         ($1::uuid, $2, $3, $4::timestamptz, $5::timestamptz,
          $6::date, 'generated', $7, $8::jsonb, NOW(),
          'stm_v1', $9::jsonb, NOW(), NOW()))");

  // Count daily summaries in past 7 days (for weekly aggregation)
  writer.prepare("stm_summary_weekly_food_avg",
    R"(SELECT COALESCE(AVG((payload->>'food_grams')::numeric), 0)
       FROM oro_base_summary
       WHERE device_id = $1::uuid AND summary_type = 'daily'
         AND summary_date >= CURRENT_DATE - INTERVAL '7 days')");

  writer.prepare("stm_summary_weekly_water_avg",
    R"(SELECT COALESCE(AVG((payload->>'water_ml')::numeric), 0)
       FROM oro_base_summary
       WHERE device_id = $1::uuid AND summary_type = 'daily'
         AND summary_date >= CURRENT_DATE - INTERVAL '7 days')");

  writer.prepare("stm_summary_weekly_event_total",
    R"(SELECT COALESCE(SUM((payload->>'event_count')::int), 0)
       FROM oro_base_summary
       WHERE device_id = $1::uuid AND summary_type = 'daily'
         AND summary_date >= CURRENT_DATE - INTERVAL '7 days')");
}

JobResult daily_pet_summary_generator(const nlohmann::json &config,
                                      storage_handoff::StorageWriter &writer) {
  std::cout << "[SummaryJobs] daily_pet_summary_generator executing...\n";
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

  // ── Aggregate today's data ──
  double food = writer.query_double("stm_summary_food_daily", device_id);
  double water = writer.query_double("stm_summary_water_daily", device_id);
  double avg_temp = writer.query_double("stm_summary_avg_temp_daily", device_id);
  double avg_hum = writer.query_double("stm_summary_avg_humidity_daily", device_id);
  int events = writer.query_int("stm_summary_event_count_daily", device_id);

  nlohmann::json payload = {
      {"food_grams", food},
      {"water_ml", water},
      {"avg_temperature", avg_temp},
      {"avg_humidity", avg_hum},
      {"event_count", events}};

  nlohmann::json signal_window = {
      {"from", "today_start"}, {"to", "today_end"}};

  // Use today's date as ISO strings for period
  auto now = std::chrono::system_clock::now();
  auto now_t = std::chrono::system_clock::to_time_t(now);
  struct tm tm_now;
  localtime_r(&now_t, &tm_now);
  char date_buf[32];
  strftime(date_buf, sizeof(date_buf), "%Y-%m-%d", &tm_now);
  std::string today_str(date_buf);
  std::string period_start = today_str + "T00:00:00Z";
  std::string period_end = today_str + "T23:59:59Z";

  writer.execute_prepared("stm_summary_insert",
      device_id,
      std::optional<std::string>{}, // dog_id
      std::string("daily"),
      period_start, period_end, today_str,
      std::string("Daily Summary — " + today_str),
      payload.dump(), signal_window.dump());

  result.items_processed = 1;
  result.metadata = payload;
  std::cout << "[SummaryJobs] Daily summary generated: food=" << food
            << "g, water=" << water << "ml, events=" << events << "\n";
  return result;
}

JobResult weekly_pet_summary_generator(const nlohmann::json &config,
                                       storage_handoff::StorageWriter &writer) {
  std::cout << "[SummaryJobs] weekly_pet_summary_generator executing...\n";
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

  // ── Aggregate from daily summaries ──
  double avg_food = writer.query_double("stm_summary_weekly_food_avg", device_id);
  double avg_water = writer.query_double("stm_summary_weekly_water_avg", device_id);
  int total_events = writer.query_int("stm_summary_weekly_event_total", device_id);

  nlohmann::json payload = {
      {"avg_daily_food_grams", avg_food},
      {"avg_daily_water_ml", avg_water},
      {"total_events_7d", total_events}};

  nlohmann::json signal_window = {{"from", "7_days_ago"}, {"to", "today"}};

  auto now = std::chrono::system_clock::now();
  auto now_t = std::chrono::system_clock::to_time_t(now);
  struct tm tm_now;
  localtime_r(&now_t, &tm_now);
  char date_buf[32];
  strftime(date_buf, sizeof(date_buf), "%Y-%m-%d", &tm_now);
  std::string today_str(date_buf);

  writer.execute_prepared("stm_summary_insert",
      device_id,
      std::optional<std::string>{},
      std::string("weekly"),
      std::string(today_str + "T00:00:00Z"),
      std::string(today_str + "T23:59:59Z"),
      today_str,
      std::string("Weekly Summary — " + today_str),
      payload.dump(), signal_window.dump());

  result.items_processed = 1;
  result.metadata = payload;
  std::cout << "[SummaryJobs] Weekly summary: avg food=" << avg_food
            << "g/day, avg water=" << avg_water
            << "ml/day, events=" << total_events << "\n";
  return result;
}

} // namespace oro::stm::jobs
