#include "scheduled_task_manager/jobs/cleanup_jobs.hpp"
#include <iostream>
#include <filesystem>
#include <chrono>
#include <string>

namespace oro::stm::jobs {

JobResult data_cleanup(const nlohmann::json& config,
                       storage_handoff::StorageWriter& writer) {
    int retention_days = config.value("/scheduled_task_manager/retention_days"_json_pointer, 30);
    std::cout << "[CleanupJobs] Starting data_cleanup with retention_days: " << retention_days << "\n";

    JobResult res;
    res.success = true;
    res.items_processed = 0;

    std::string days_str = std::to_string(retention_days);

    // Run database prunes
    // Signals & events are gated by summary coverage first (raw data is
    // disposable once summarised), then by the retention_days hard cap.
    int signals_deleted      = writer.execute_prepared_count("stm_cleanup_signals",      days_str);
    int events_deleted       = writer.execute_prepared_count("stm_cleanup_events",       days_str);
    int notifications_deleted= writer.execute_prepared_count("stm_cleanup_notifications",days_str);
    int executions_deleted   = writer.execute_prepared_count("stm_cleanup_executions",   days_str);
    int retry_deleted        = writer.execute_prepared_count("stm_cleanup_retry_queue",  days_str);

    int total_db_deleted = signals_deleted + events_deleted + notifications_deleted + executions_deleted + retry_deleted;
    std::cout << "[CleanupJobs] Database pruning complete:\n"
              << "  - signals deleted (summarised periods + >" << retention_days << "d fallback): " << signals_deleted << "\n"
              << "  - events deleted  (summarised periods + >" << retention_days << "d fallback): " << events_deleted << "\n"
              << "  - notifications deleted: " << notifications_deleted << "\n"
              << "  - job executions deleted: " << executions_deleted << "\n"
              << "  - retry dead-letters deleted: " << retry_deleted << "\n"
              << "  Total DB rows deleted: " << total_db_deleted << "\n";

    // Run disk prune
    int files_deleted = 0;
    std::string media_dir = "/home/radxa/oro_base_video_audio";
    namespace fs = std::filesystem;
    if (fs::exists(media_dir) && fs::is_directory(media_dir)) {
        try {
            auto now_file_time = fs::file_time_type::clock::now();
            for (const auto& entry : fs::directory_iterator(media_dir)) {
                if (entry.is_regular_file()) {
                    auto mtime = fs::last_write_time(entry);
                    auto age_duration = now_file_time - mtime;
                    auto age_hours = std::chrono::duration_cast<std::chrono::hours>(age_duration).count();
                    
                    if (age_hours > retention_days * 24) {
                        fs::remove(entry.path());
                        files_deleted++;
                    }
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "[CleanupJobs] Error during disk media cleanup: " << e.what() << "\n";
            res.success = false;
            res.error = std::string("Disk cleanup error: ") + e.what();
        }
    } else {
        std::cout << "[CleanupJobs] Media directory " << media_dir << " does not exist or is not a directory. Skipping disk cleanup.\n";
    }

    std::cout << "[CleanupJobs] Disk media pruning complete. Files deleted: " << files_deleted << "\n";

    res.items_processed = total_db_deleted + files_deleted;
    res.metadata["signals_deleted"] = signals_deleted;
    res.metadata["events_deleted"] = events_deleted;
    res.metadata["notifications_deleted"] = notifications_deleted;
    res.metadata["executions_deleted"] = executions_deleted;
    res.metadata["retry_deleted"] = retry_deleted;
    res.metadata["files_deleted"] = files_deleted;
    res.metadata["retention_days"] = retention_days;

    return res;
}

void prepare_cleanup_job_statements(storage_handoff::StorageWriter& writer) {
    // 1. stm_cleanup_signals
    // Deletes signals that satisfy EITHER condition:
    //   (a) The observed_at period is covered by a generated daily summary for
    //       the same device — raw telemetry is disposable once summarised.
    //   (b) The row is older than retention_days (hard fallback, catches devices
    //       that never generated a summary).
    // In both cases the 5 most-recent rows per (device_id, signal_id) are
    // protected to preserve the latest configuration baseline.
    writer.prepare("stm_cleanup_signals", R"(
        DELETE FROM public.oro_base_signals s
        WHERE (
            -- (a) period is covered by a generated daily summary
            EXISTS (
                SELECT 1
                FROM public.oro_base_summary sum
                WHERE sum.device_id    = s.device_id
                  AND sum.summary_type = 'daily'
                  AND sum.status       = 'generated'
                  AND s.observed_at   >= sum.period_start
                  AND s.observed_at   <  sum.period_end
            )
            OR
            -- (b) hard retention fallback
            s.observed_at < NOW() - ($1 || ' days')::interval
        )
        -- always preserve the latest 5 readings per (device, signal_type)
        AND s.id NOT IN (
            SELECT id FROM (
                SELECT id, ROW_NUMBER() OVER (
                    PARTITION BY device_id, signal_id
                    ORDER BY observed_at DESC
                ) AS rn
                FROM public.oro_base_signals
            ) ranked
            WHERE rn <= 5
        )
    )");

    // 2. stm_cleanup_events
    // Same dual-gate: summarised periods OR beyond hard retention cap.
    writer.prepare("stm_cleanup_events", R"(
        DELETE FROM public.oro_base_events e
        WHERE (
            -- (a) period covered by a generated daily summary
            EXISTS (
                SELECT 1
                FROM public.oro_base_summary sum
                WHERE sum.device_id    = e.device_id
                  AND sum.summary_type = 'daily'
                  AND sum.status       = 'generated'
                  AND e.detected_at   >= sum.period_start
                  AND e.detected_at   <  sum.period_end
            )
            OR
            -- (b) hard retention fallback
            e.detected_at < NOW() - ($1 || ' days')::interval
        )
    )");

    // 3. stm_cleanup_notifications
    writer.prepare("stm_cleanup_notifications", R"(
        DELETE FROM public.oro_base_notifications
        WHERE generated_at < NOW() - ($1 || ' days')::interval
    )");

    // 4. stm_cleanup_executions
    writer.prepare("stm_cleanup_executions", R"(
        DELETE FROM public.stm_job_executions
        WHERE started_at < NOW() - ($1 || ' days')::interval
    )");

    // 5. stm_cleanup_retry_queue
    writer.prepare("stm_cleanup_retry_queue", R"(
        DELETE FROM public.stm_retry_queue
        WHERE status = 'dead_letter' AND updated_at < NOW() - ($1 || ' days')::interval
    )");
}

} // namespace oro::stm::jobs
