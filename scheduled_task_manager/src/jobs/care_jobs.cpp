#include "scheduled_task_manager/jobs/care_jobs.hpp"
#include <chrono>
#include <iostream>

namespace oro::stm::jobs {

void prepare_care_job_statements(storage_handoff::StorageWriter &writer) {
  // Count feeding schedules due now (within current minute)
  writer.prepare("stm_care_feeding_due_count",
    R"(SELECT COUNT(*) FROM oro_base_feeding_schedules
       WHERE device_id = $1::uuid AND is_active = true
         AND scheduled_time BETWEEN (CURRENT_TIME - INTERVAL '1 minute') AND CURRENT_TIME)");

  // Count care schedules due today (medication, grooming, vaccination, deworming)
  writer.prepare("stm_care_schedule_due_count",
    R"(SELECT COUNT(*) FROM oro_base_care_schedules
       WHERE device_id = $1::uuid AND is_active = true
         AND (
           (due_date IS NOT NULL AND due_date = CURRENT_DATE)
           OR
           (scheduled_time IS NOT NULL
            AND scheduled_time BETWEEN (CURRENT_TIME - INTERVAL '1 minute') AND CURRENT_TIME)
         )
         AND (last_completed_at IS NULL OR last_completed_at::date < CURRENT_DATE))");

  // Count overdue feeding schedules (past scheduled_time + 15 min grace)
  writer.prepare("stm_care_overdue_feeding_count",
    R"(SELECT COUNT(*) FROM oro_base_feeding_schedules
       WHERE device_id = $1::uuid AND is_active = true
         AND scheduled_time < (CURRENT_TIME - INTERVAL '15 minutes'))");

  // Count overdue care schedules
  writer.prepare("stm_care_overdue_schedule_count",
    R"(SELECT COUNT(*) FROM oro_base_care_schedules
       WHERE device_id = $1::uuid AND is_active = true
         AND (
           (due_date IS NOT NULL AND due_date < CURRENT_DATE)
           OR
           (scheduled_time IS NOT NULL
            AND scheduled_time < (CURRENT_TIME - INTERVAL '15 minutes'))
         )
         AND (last_completed_at IS NULL OR last_completed_at::date < CURRENT_DATE))");

  // Insert a care reminder event into oro_base_events
  writer.prepare("stm_care_emit_event",
    R"(INSERT INTO oro_base_events
         (device_id, dog_id, event_type, category, event_source, severity,
          status, trigger_mode, detected_at, event_start_at, confidence,
          title, description, payload, dedupe_key, notification_eligible,
          created_at, updated_at)
       VALUES
         ($1::uuid, $2::uuid, $3, 'Care', 'scheduled_task_manager', $4,
          'open', 'scheduled', NOW(), NOW(), NULL,
          $5, $6, $7::jsonb, $8, true, NOW(), NOW()))");

  // Emit due feeding notification
  writer.prepare("stm_care_emit_feeding_notification",
    R"(INSERT INTO oro_base_notifications (
         device_id, dog_id, user_id, notification_type, category, notification_key,
         title, message, priority, status, delivery_channel, trigger_mode,
         scheduled_for, generated_at, payload, dedupe_key, expires_at
       )
       SELECT 
         f.device_id, 
         f.dog_id, 
         COALESCE(f.created_by_user_id, u.user_id), 
         'reminder', 
         'Feeding', 
         'feeding_reminder_' || f.feeding_schedule_id, 
         'Time to feed ' || d.name, 
         'It is time for ' || d.name || 's ' || COALESCE(f.meal_name, 'meal') || ' (' || f.portion_grams || 'g).', 
         'medium', 
         'pending', 
         'in_app', 
         'scheduled', 
         NOW(), 
         NOW(), 
         jsonb_build_object('feeding_schedule_id', f.feeding_schedule_id, 'meal_name', f.meal_name, 'portion_grams', f.portion_grams), 
         'FEED_REMIND_' || f.feeding_schedule_id || '_' || TO_CHAR(CURRENT_DATE, 'YYYYMMDD'),
         NOW() + INTERVAL '24 hours'
       FROM oro_base_feeding_schedules f
       JOIN oro_base_dog d ON f.dog_id = d.dog_id
       LEFT JOIN oro_base_user u ON f.device_id = u.device_id
       WHERE f.device_id = $1::uuid AND f.is_active = true
         AND f.scheduled_time BETWEEN (CURRENT_TIME - INTERVAL '1 minute') AND CURRENT_TIME
         AND NOT EXISTS (
           SELECT 1 FROM oro_base_notifications 
           WHERE dedupe_key = 'FEED_REMIND_' || f.feeding_schedule_id || '_' || TO_CHAR(CURRENT_DATE, 'YYYYMMDD')
         ))");

  // Emit due care notification
  writer.prepare("stm_care_emit_care_notification",
    R"(INSERT INTO oro_base_notifications (
         device_id, dog_id, user_id, notification_type, category, notification_key,
         title, message, priority, status, delivery_channel, trigger_mode,
         scheduled_for, generated_at, payload, dedupe_key, expires_at
       )
       SELECT 
         c.device_id, 
         c.dog_id, 
         COALESCE(c.created_by_user_id, u.user_id), 
         'reminder', 
         COALESCE(c.care_type, 'Care'), 
         'care_reminder_' || c.care_schedule_id, 
         'Care task: ' || COALESCE(c.title, 'Reminder'), 
         'Reminder: ' || COALESCE(c.description, 'It is time for a scheduled care task.'), 
         'medium', 
         'pending', 
         'in_app', 
         'scheduled', 
         NOW(), 
         NOW(), 
         jsonb_build_object('care_schedule_id', c.care_schedule_id, 'care_type', c.care_type, 'title', c.title), 
         'CARE_REMIND_' || c.care_schedule_id || '_' || TO_CHAR(CURRENT_DATE, 'YYYYMMDD'),
         NOW() + INTERVAL '24 hours'
       FROM oro_base_care_schedules c
       JOIN oro_base_dog d ON c.dog_id = d.dog_id
       LEFT JOIN oro_base_user u ON c.device_id = u.device_id
       WHERE c.device_id = $1::uuid AND c.is_active = true
         AND (
           (c.due_date IS NOT NULL AND c.due_date = CURRENT_DATE)
           OR
           (c.scheduled_time IS NOT NULL 
            AND c.scheduled_time BETWEEN (CURRENT_TIME - INTERVAL '1 minute') AND CURRENT_TIME)
         )
         AND (c.last_completed_at IS NULL OR c.last_completed_at::date < CURRENT_DATE)
         AND NOT EXISTS (
           SELECT 1 FROM oro_base_notifications 
           WHERE dedupe_key = 'CARE_REMIND_' || c.care_schedule_id || '_' || TO_CHAR(CURRENT_DATE, 'YYYYMMDD')
         ))");

  // Emit overdue feeding notification
  writer.prepare("stm_care_emit_overdue_feeding_notification",
    R"(INSERT INTO oro_base_notifications (
         device_id, dog_id, user_id, notification_type, category, notification_key,
         title, message, priority, status, delivery_channel, trigger_mode,
         scheduled_for, generated_at, payload, dedupe_key, expires_at
       )
       SELECT 
         f.device_id, 
         f.dog_id, 
         COALESCE(f.created_by_user_id, u.user_id), 
         'reminder', 
         'Feeding', 
         'feeding_overdue_' || f.feeding_schedule_id, 
         'Overdue feeding: ' || COALESCE(f.meal_name, 'meal'), 
         'Feeding schedule for ' || d.name || ' is overdue.', 
         'high', 
         'pending', 
         'in_app', 
         'scheduled', 
         NOW(), 
         NOW(), 
         jsonb_build_object('feeding_schedule_id', f.feeding_schedule_id, 'meal_name', f.meal_name), 
         'FEED_OVERDUE_' || f.feeding_schedule_id || '_' || TO_CHAR(CURRENT_DATE, 'YYYYMMDD'),
         NOW() + INTERVAL '24 hours'
       FROM oro_base_feeding_schedules f
       JOIN oro_base_dog d ON f.dog_id = d.dog_id
       LEFT JOIN oro_base_user u ON f.device_id = u.device_id
       WHERE f.device_id = $1::uuid AND f.is_active = true
         AND f.scheduled_time < (CURRENT_TIME - INTERVAL '15 minutes')
         AND NOT EXISTS (
           SELECT 1 FROM oro_base_notifications 
           WHERE dedupe_key = 'FEED_OVERDUE_' || f.feeding_schedule_id || '_' || TO_CHAR(CURRENT_DATE, 'YYYYMMDD')
         ))");

  // Emit overdue care notification
  writer.prepare("stm_care_emit_overdue_care_notification",
    R"(INSERT INTO oro_base_notifications (
         device_id, dog_id, user_id, notification_type, category, notification_key,
         title, message, priority, status, delivery_channel, trigger_mode,
         scheduled_for, generated_at, payload, dedupe_key, expires_at
       )
       SELECT 
         c.device_id, 
         c.dog_id, 
         COALESCE(c.created_by_user_id, u.user_id), 
         'reminder', 
         COALESCE(c.care_type, 'Care'), 
         'care_overdue_' || c.care_schedule_id, 
         'Overdue care task: ' || COALESCE(c.title, 'Reminder'), 
         'Care task ' || COALESCE(c.title, 'Reminder') || ' for ' || d.name || ' is overdue.', 
         'high', 
         'pending', 
         'in_app', 
         'scheduled', 
         NOW(), 
         NOW(), 
         jsonb_build_object('care_schedule_id', c.care_schedule_id, 'care_type', c.care_type, 'title', c.title), 
         'CARE_OVERDUE_' || c.care_schedule_id || '_' || TO_CHAR(CURRENT_DATE, 'YYYYMMDD'),
         NOW() + INTERVAL '24 hours'
       FROM oro_base_care_schedules c
       JOIN oro_base_dog d ON c.dog_id = d.dog_id
       LEFT JOIN oro_base_user u ON c.device_id = u.device_id
       WHERE c.device_id = $1::uuid AND c.is_active = true
         AND (
           (c.due_date IS NOT NULL AND c.due_date < CURRENT_DATE)
           OR
           (c.scheduled_time IS NOT NULL 
            AND c.scheduled_time < (CURRENT_TIME - INTERVAL '15 minutes'))
         )
         AND (c.last_completed_at IS NULL OR c.last_completed_at::date < CURRENT_DATE)
         AND NOT EXISTS (
           SELECT 1 FROM oro_base_notifications 
           WHERE dedupe_key = 'CARE_OVERDUE_' || c.care_schedule_id || '_' || TO_CHAR(CURRENT_DATE, 'YYYYMMDD')
         ))");
}

JobResult care_reminder_dispatch(const nlohmann::json &config,
                                 storage_handoff::StorageWriter &writer) {
  std::cout << "[CareJobs] care_reminder_dispatch executing...\n";
  JobResult result;
  result.success = true;
  result.items_processed = 0;

  std::string device_id =
      config.value("/global/device_id"_json_pointer, std::string(""));

  if (device_id.empty()) {
    result.error = "No device_id in config";
    result.success = false;
    return result;
  }

  // ── Emit feeding reminder notifications due now ──
  int feeding_due = writer.execute_prepared_count("stm_care_emit_feeding_notification", device_id);
  if (feeding_due > 0) {
    std::cout << "[CareJobs] " << feeding_due
              << " feeding reminder notification(s) dispatched.\n";
    result.items_processed += feeding_due;
    result.metadata["feeding_reminders"] = feeding_due;
  }

  // ── Emit care reminder notifications due today ──
  int care_due = writer.execute_prepared_count("stm_care_emit_care_notification", device_id);
  if (care_due > 0) {
    std::cout << "[CareJobs] " << care_due << " care reminder notification(s) dispatched.\n";
    result.items_processed += care_due;
    result.metadata["care_reminders"] = care_due;
  }

  // ── Also check config-based meal schedules (fallback) ──
  if (config.contains("meal_schedules")) {
    auto now = std::chrono::system_clock::now();
    auto now_t = std::chrono::system_clock::to_time_t(now);
    struct tm *tm_now = localtime(&now_t);
    int current_minutes = tm_now->tm_hour * 60 + tm_now->tm_min;

    for (auto &[meal_name, meal_config] : config["meal_schedules"].items()) {
      if (!meal_config.contains("start_time"))
        continue;
      std::string start_str = meal_config["start_time"].get<std::string>();
      int start_hour = 0, start_min = 0;
      if (sscanf(start_str.c_str(), "%d:%d", &start_hour, &start_min) == 2) {
        int meal_minutes = start_hour * 60 + start_min;
        if (current_minutes >= meal_minutes &&
            current_minutes < meal_minutes + 1) {
          std::cout << "[CareJobs] Config meal '" << meal_name << "' due.\n";
          result.items_processed++;
        }
      }
    }
  }

  std::cout << "[CareJobs] care_reminder_dispatch done. "
            << result.items_processed << " reminder(s).\n";
  return result;
}

JobResult overdue_task_checker(const nlohmann::json &config,
                               storage_handoff::StorageWriter &writer) {
  std::cout << "[CareJobs] overdue_task_checker executing...\n";
  JobResult result;
  result.success = true;
  result.items_processed = 0;

  std::string device_id =
      config.value("/global/device_id"_json_pointer, std::string(""));

  if (device_id.empty()) {
    result.error = "No device_id in config";
    result.success = false;
    return result;
  }

  // ── Check and emit overdue feeding notifications ──
  int overdue_feeding = writer.execute_prepared_count("stm_care_emit_overdue_feeding_notification", device_id);
  if (overdue_feeding > 0) {
    std::cout << "[CareJobs] " << overdue_feeding
              << " overdue feeding notification(s) dispatched.\n";
    result.items_processed += overdue_feeding;
    result.metadata["overdue_feedings"] = overdue_feeding;

    // Emit overdue event as well for historic event trail
    nlohmann::json payload = {{"overdue_count", overdue_feeding},
                              {"type", "feeding"},
                              {"checked_at", std::chrono::duration_cast<
                                   std::chrono::milliseconds>(
                                   std::chrono::system_clock::now()
                                       .time_since_epoch()).count()}};
    writer.execute_prepared("stm_care_emit_event",
        device_id,
        std::optional<std::string>{},  // dog_id nullable
        std::string("feeding_overdue"),
        std::string("medium"),
        std::string("Overdue Feeding Schedule"),
        std::string("One or more feeding schedules are past due."),
        payload.dump(),
        "STM_OVERDUE_FEED_" + device_id);
  }

  // ── Check and emit overdue care notifications ──
  int overdue_care = writer.execute_prepared_count("stm_care_emit_overdue_care_notification", device_id);
  if (overdue_care > 0) {
    std::cout << "[CareJobs] " << overdue_care
              << " overdue care notification(s) dispatched.\n";
    result.items_processed += overdue_care;
    result.metadata["overdue_care"] = overdue_care;

    nlohmann::json payload = {{"overdue_count", overdue_care},
                              {"type", "care"}};
    writer.execute_prepared("stm_care_emit_event",
        device_id,
        std::optional<std::string>{},
        std::string("care_task_overdue"),
        std::string("medium"),
        std::string("Overdue Care Task"),
        std::string("One or more care schedules are overdue."),
        payload.dump(),
        "STM_OVERDUE_CARE_" + device_id);
  }

  std::cout << "[CareJobs] overdue_task_checker done. "
            << result.items_processed << " overdue.\n";
  return result;
}

} // namespace oro::stm::jobs
