#include "scheduled_task_manager/jobs/care_jobs.hpp"
#include <chrono>
#include <iostream>
#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

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

  // Insert a care reminder notification (configurable priority/trigger_mode and deduplication key check)
  writer.prepare("stm_care_insert_notification",
    R"(INSERT INTO oro_base_notifications (
         device_id, dog_id, user_id, notification_type, category, notification_key,
         title, message, priority, status, delivery_channel, trigger_mode,
         scheduled_for, generated_at, payload, dedupe_key, expires_at, created_at, updated_at
       )
       SELECT 
         $1::uuid, 
         $2::uuid, 
         COALESCE($3::uuid, (SELECT user_id FROM oro_base_user WHERE device_id = $1::uuid LIMIT 1)), 
         'reminder',
         $4, 
         $5, 
         $6, 
         $7, 
         $8, 
         'pending',
         'in_app',
         $9, 
         NOW(),
         NOW(),
         $10::jsonb, 
         $11,
         NOW() + INTERVAL '24 hours',
         NOW(),
         NOW()
       WHERE NOT EXISTS (
         SELECT 1 FROM oro_base_notifications 
         WHERE dedupe_key = $11
       ))");

  // Fetch active care schedules due now
  writer.prepare("stm_care_fetch_due_schedules",
    R"(SELECT 
         c.care_schedule_id,
         c.dog_id,
         c.device_id,
         c.care_type,
         c.title,
         c.description,
         c.created_by_user_id
       FROM oro_base_care_schedules c
       WHERE c.is_active = true
         AND c.device_id = $1::uuid
         AND (c.scheduled_time IS NULL 
              OR c.scheduled_time BETWEEN (CURRENT_TIME - INTERVAL '1 minute') AND CURRENT_TIME)
         AND (
              (c.recurrence_type = 'one_time' AND c.due_date = CURRENT_DATE)
              OR (c.recurrence_type = 'daily')
              OR (c.recurrence_type = 'weekly' AND 
                  (c.recurrence_days ? LOWER(TRIM(TO_CHAR(CURRENT_DATE, 'Day')))))
              OR (c.recurrence_type = 'monthly' AND EXTRACT(DAY FROM c.start_date) = EXTRACT(DAY FROM CURRENT_DATE))
         )
         AND (c.last_completed_at IS NULL OR c.last_completed_at::date < CURRENT_DATE)
         AND NOT EXISTS (
             SELECT 1 
             FROM oro_base_notifications n
             WHERE n.dedupe_key = 'CARE_REMIND_' || c.care_schedule_id || '_' || TO_CHAR(CURRENT_DATE, 'YYYYMMDD')
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

  // Resolve Job-level Defaults for Priority and Trigger Mode
  std::string default_priority = "medium";
  std::string default_trigger_mode = "scheduled";

  if (config.contains("scheduled_task_manager") && 
      config["scheduled_task_manager"].contains("jobs") &&
      config["scheduled_task_manager"]["jobs"].contains("care_reminder_dispatch")) {
      
      auto job_cfg = config["scheduled_task_manager"]["jobs"]["care_reminder_dispatch"];
      default_priority = job_cfg.value("default_priority", default_priority);
      default_trigger_mode = job_cfg.value("default_trigger_mode", default_trigger_mode);
  }

  // =========================================================================
  // MODE 1: Config-based Validation Mode (Modular, can be commented out later)
  // =========================================================================
  bool run_config_validation = true; // Flag to enable/disable validation mode
  if (run_config_validation) {
    std::cout << "[CareJobs] [Mode 1] Evaluating config-based schedules...\n";
    auto now = std::chrono::system_clock::now();
    auto now_t = std::chrono::system_clock::to_time_t(now);
    struct tm *tm_now = localtime(&now_t);
    char date_buf[16];
    strftime(date_buf, sizeof(date_buf), "%Y-%m-%d", tm_now);
    std::string current_date_str(date_buf);
    
    char date_buf_key[16];
    strftime(date_buf_key, sizeof(date_buf_key), "%Y%m%d", tm_now);
    std::string date_key_str(date_buf_key);

    if (config.contains("scheduled_task_manager") && 
        config["scheduled_task_manager"].contains("care_schedules")) {
        
        auto care_schedules_arr = config["scheduled_task_manager"]["care_schedules"];
        for (const auto& schedule : care_schedules_arr) {
            std::string care_type = schedule.value("care_type", "");
            std::string title = schedule.value("title", "");
            std::string recurrence = schedule.value("recurrence", "daily");
            std::string due_time_str = schedule.value("due_time", "");
            std::string due_date_str = schedule.value("due_date", "");
            
            std::string priority = schedule.value("priority", default_priority);
            std::string trigger_mode = schedule.value("trigger_mode", default_trigger_mode);

            // Check time match (HH:MM:SS or HH:MM)
            int scheduled_hour = 0, scheduled_min = 0, scheduled_sec = 0;
            if (sscanf(due_time_str.c_str(), "%d:%d:%d", &scheduled_hour, &scheduled_min, &scheduled_sec) >= 2) {
                if (tm_now->tm_hour == scheduled_hour && tm_now->tm_min == scheduled_min) {
                    
                    bool day_matches = false;
                    if (recurrence == "daily") {
                        day_matches = true;
                    } else if (recurrence == "weekly") {
                        std::string req_day = schedule.value("recurrence_day", "");
                        std::transform(req_day.begin(), req_day.end(), req_day.begin(), ::tolower);
                        int wday_idx = tm_now->tm_wday;
                        std::vector<std::string> days = {"sunday", "monday", "tuesday", "wednesday", "thursday", "friday", "saturday"};
                        if (req_day == days[wday_idx]) {
                            day_matches = true;
                        }
                    } else if (recurrence == "monthly") {
                        if (!due_date_str.empty()) {
                            int due_year = 0, due_month = 0, due_day = 0;
                            if (sscanf(due_date_str.c_str(), "%d-%d-%d", &due_year, &due_month, &due_day) == 3) {
                                if (tm_now->tm_mday == due_day) {
                                    day_matches = true;
                                }
                            }
                        }
                    } else if (recurrence == "quarterly") {
                        if (!due_date_str.empty()) {
                            int due_year = 0, due_month = 0, due_day = 0;
                            if (sscanf(due_date_str.c_str(), "%d-%d-%d", &due_year, &due_month, &due_day) == 3) {
                                if (tm_now->tm_mday == due_day && (tm_now->tm_mon + 1 - due_month) % 3 == 0) {
                                    day_matches = true;
                                }
                            }
                        }
                    }

                    if (!due_date_str.empty() && due_date_str == current_date_str) {
                        day_matches = true;
                    }

                    if (day_matches) {
                        std::cout << "[CareJobs] [Mode 1] Config schedule due: " << title << "\n";
                        
                        std::string dedupe_key = "CFG_CARE_REMIND_" + care_type + "_" + date_key_str;
                        nlohmann::json payload = {{"care_type", care_type}, {"source", "config_validation"}};
                        
                        writer.execute_prepared("stm_care_insert_notification",
                            device_id,
                            std::optional<std::string>{}, // dog_id
                            std::optional<std::string>{}, // user_id
                            std::string("Care"),
                            "cfg_reminder_" + care_type,
                            title,
                            "Reminder: " + title + " is scheduled for now.",
                            priority,
                            trigger_mode,
                            payload.dump(),
                            dedupe_key
                        );
                        result.items_processed++;
                    }
                }
            }
        }
    }
  }

  // =========================================================================
  // MODE 2: Database-based Production Mode (Standard flow)
  // =========================================================================
  std::cout << "[CareJobs] [Mode 2] Evaluating database-based schedules...\n";
  try {
    pqxx::result due_schedules = writer.query_result("stm_care_fetch_due_schedules", device_id);
    
    for (const auto& row : due_schedules) {
        std::string schedule_id = row["care_schedule_id"].as<std::string>();
        std::string dog_id = row["dog_id"].is_null() ? "" : row["dog_id"].as<std::string>();
        std::string req_device_id = row["device_id"].is_null() ? device_id : row["device_id"].as<std::string>();
        std::string care_type = row["care_type"].as<std::string>();
        std::string title = row["title"].as<std::string>();
        std::string description = row["description"].is_null() ? "" : row["description"].as<std::string>();
        std::string user_id = row["created_by_user_id"].is_null() ? "" : row["created_by_user_id"].as<std::string>();

        // Generate daily dedupe key
        auto now = std::chrono::system_clock::now();
        auto now_t = std::chrono::system_clock::to_time_t(now);
        struct tm *tm_now = localtime(&now_t);
        char date_buf[16];
        strftime(date_buf, sizeof(date_buf), "%Y%m%d", tm_now);
        std::string dedupe_key = "CARE_REMIND_" + schedule_id + "_" + std::string(date_buf);

        nlohmann::json payload = {
            {"care_schedule_id", schedule_id},
            {"care_type", care_type},
            {"title", title}
        };

        std::string message = description.empty() ? 
                              ("It is time for a scheduled care task: " + title) : description;

        // Execute insert
        writer.execute_prepared("stm_care_insert_notification",
            req_device_id,
            dog_id.empty() ? std::optional<std::string>{} : std::optional<std::string>{dog_id},
            user_id.empty() ? std::optional<std::string>{} : std::optional<std::string>{user_id},
            std::string("Care"),
            "care_reminder_" + schedule_id,
            "Care Task: " + title,
            message,
            default_priority,
            default_trigger_mode,
            payload.dump(),
            dedupe_key
        );

        result.items_processed++;
    }
    
  } catch (const std::exception &e) {
    std::cerr << "[CareJobs] Mode 2 DB execution error: " << e.what() << "\n";
    result.success = false;
    result.error = e.what();
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

  // ── 1. Set session timezone ───────────────────────────────────────────────
  // Ensure all SQL date/time comparisons (CURRENT_TIME, CURRENT_DATE, NOW())
  // respect the user's configured timezone rather than the server default.
  std::string tz = config.value("/global/timezone"_json_pointer, std::string("UTC"));
  writer.set_session_timezone(tz);

  // ── 2. Resolve configurable grace period ──────────────────────────────────
  int grace_minutes = 15; // default: 15 minutes
  if (config.contains("scheduled_task_manager") && 
      config["scheduled_task_manager"].contains("jobs") &&
      config["scheduled_task_manager"]["jobs"].contains("overdue_task_checker")) {
      grace_minutes = config["scheduled_task_manager"]["jobs"]["overdue_task_checker"]
                          .value("grace_period_minutes", 15);
  }
  std::cout << "[CareJobs] Grace period: " << grace_minutes << " minutes, timezone: " << tz << "\n";

  // ── 3. Mark overdue — Option B: Track via events only ─────────────────────
  //
  // TODO: [Option A — Production] Add a `status` column to oro_base_care_schedules
  //       and oro_base_feeding_schedules for direct overdue marking:
  //
  //   ALTER TABLE oro_base_care_schedules
  //     ADD COLUMN IF NOT EXISTS status TEXT NOT NULL DEFAULT 'pending';
  //   -- Valid values: pending, completed, overdue, skipped
  //
  //   ALTER TABLE oro_base_feeding_schedules
  //     ADD COLUMN IF NOT EXISTS status TEXT NOT NULL DEFAULT 'pending';
  //
  //   CREATE INDEX IF NOT EXISTS ix_care_schedule_status
  //     ON oro_base_care_schedules(status) WHERE status = 'overdue';
  //   CREATE INDEX IF NOT EXISTS ix_feeding_schedule_status
  //     ON oro_base_feeding_schedules(status) WHERE status = 'overdue';
  //
  // TODO: [Option A] Add prepared statements:
  //   writer.prepare("stm_care_mark_overdue_care",
  //     R"(UPDATE oro_base_care_schedules
  //        SET status = 'overdue', updated_at = NOW()
  //        WHERE device_id = $1::uuid AND is_active = true
  //          AND status != 'overdue'
  //          AND (
  //            (due_date IS NOT NULL AND due_date < CURRENT_DATE)
  //            OR
  //            (scheduled_time IS NOT NULL
  //             AND scheduled_time < (CURRENT_TIME - ($2 || ' minutes')::interval))
  //          )
  //          AND (last_completed_at IS NULL OR last_completed_at::date < CURRENT_DATE))");
  //
  //   writer.prepare("stm_care_mark_overdue_feeding",
  //     R"(UPDATE oro_base_feeding_schedules
  //        SET status = 'overdue', updated_at = NOW()
  //        WHERE device_id = $1::uuid AND is_active = true
  //          AND status != 'overdue'
  //          AND scheduled_time < (CURRENT_TIME - ($2 || ' minutes')::interval))");
  //
  // TODO: [Option A] Execute mark-overdue UPDATEs before notification emission:
  //   std::string grace_str = std::to_string(grace_minutes);
  //   int marked_care = writer.execute_prepared_count("stm_care_mark_overdue_care", device_id, grace_str);
  //   int marked_feeding = writer.execute_prepared_count("stm_care_mark_overdue_feeding", device_id, grace_str);
  //
  // TODO: [Option A] Add daily EOD status reset job:
  //   writer.prepare("stm_care_reset_daily_status",
  //     R"(UPDATE oro_base_care_schedules
  //        SET status = 'pending', updated_at = NOW()
  //        WHERE device_id = $1::uuid AND is_active = true
  //          AND status = 'overdue'
  //          AND last_completed_at IS NOT NULL
  //          AND last_completed_at::date = CURRENT_DATE)");
  //

  int overdue_feeding = 0;
  int overdue_care = 0;

  // ── 4. Check and emit overdue feeding notifications ───────────────────────
  try {
    overdue_feeding = writer.execute_prepared_count(
        "stm_care_emit_overdue_feeding_notification", device_id);
    if (overdue_feeding > 0) {
      std::cout << "[CareJobs] " << overdue_feeding
                << " overdue feeding notification(s) dispatched.\n";
      result.items_processed += overdue_feeding;

      // Emit overdue event to oro_base_events (Option B: events as overdue markers)
      nlohmann::json payload = {{"overdue_count", overdue_feeding},
                                {"type", "feeding"},
                                {"grace_period_minutes", grace_minutes},
                                {"timezone", tz},
                                {"checked_at", std::chrono::duration_cast<
                                     std::chrono::milliseconds>(
                                     std::chrono::system_clock::now()
                                         .time_since_epoch()).count()}};
      writer.execute_prepared("stm_care_emit_event",
          device_id,
          std::optional<std::string>{},
          std::string("feeding_overdue"),
          std::string("medium"),
          std::string("Overdue Feeding Schedule"),
          std::string("One or more feeding schedules are past due."),
          payload.dump(),
          "STM_OVERDUE_FEED_" + device_id);
    }
  } catch (const std::exception &e) {
    std::cerr << "[CareJobs] Overdue feeding check error: " << e.what() << "\n";
    result.success = false;
    result.error = std::string("Feeding check failed: ") + e.what();
  }

  // ── 5. Check and emit overdue care notifications ──────────────────────────
  try {
    overdue_care = writer.execute_prepared_count(
        "stm_care_emit_overdue_care_notification", device_id);
    if (overdue_care > 0) {
      std::cout << "[CareJobs] " << overdue_care
                << " overdue care notification(s) dispatched.\n";
      result.items_processed += overdue_care;

      nlohmann::json payload = {{"overdue_count", overdue_care},
                                {"type", "care"},
                                {"grace_period_minutes", grace_minutes},
                                {"timezone", tz}};
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
  } catch (const std::exception &e) {
    std::cerr << "[CareJobs] Overdue care check error: " << e.what() << "\n";
    if (result.error.empty()) {
      result.error = std::string("Care check failed: ") + e.what();
    }
    result.success = false;
  }

  // ── 6. Populate result metadata for observability ─────────────────────────
  result.metadata["overdue_feedings"] = overdue_feeding;
  result.metadata["overdue_care"] = overdue_care;
  result.metadata["grace_period_minutes"] = grace_minutes;
  result.metadata["timezone"] = tz;

  // ── 7. Structured observability log line ───────────────────────────────────
  std::cout << "[CareJobs] [METRIC] overdue_task_checker"
            << " overdue_feeding=" << overdue_feeding
            << " overdue_care=" << overdue_care
            << " grace_minutes=" << grace_minutes
            << " tz=" << tz
            << " total_items=" << result.items_processed
            << "\n";

  std::cout << "[CareJobs] overdue_task_checker done. "
            << result.items_processed << " overdue.\n";
  return result;
}

} // namespace oro::stm::jobs

