#include "scheduled_task_manager/jobs/cleanup_jobs.hpp"
#include <iostream>

namespace oro::stm::jobs {

void prepare_cleanup_job_statements(storage_handoff::StorageWriter &writer) {
  // Cleanup expired notifications
  writer.prepare("stm_cleanup_expired_notifications",
    R"(DELETE FROM oro_base_notifications
       WHERE expires_at IS NOT NULL AND expires_at < NOW())");
}

JobResult data_cleanup(const nlohmann::json &config,
                       storage_handoff::StorageWriter &writer) {
  std::cout << "[CleanupJobs] data_cleanup executing...\n";
  JobResult result;
  result.success = true;
  result.items_processed = 0;

  int retention_days = 30;
  if (config.contains("scheduled_task_manager") &&
      config["scheduled_task_manager"].contains("retention_days")) {
    retention_days =
        config["scheduled_task_manager"]["retention_days"].get<int>();
  }

  // ── 1. Clean expired notifications ──
  int cleaned =
      writer.execute_prepared_count("stm_cleanup_expired_notifications");
  if (cleaned > 0) {
    std::cout << "[CleanupJobs] Cleaned " << cleaned
              << " expired notification(s).\n";
    result.items_processed += cleaned;
  }

  // TODO: Orphaned media file cleanup:
  //   - Scan /home/radxa/Pictures/Command_Executor_Images/
  //   - Scan /home/radxa/Videos/Command_Executor_Videos/
  //   - Cross-reference with DB; delete orphans older than retention_days

  result.metadata["retention_days"] = retention_days;
  std::cout << "[CleanupJobs] data_cleanup done. " << result.items_processed
            << " record(s) cleaned.\n";
  return result;
}

} // namespace oro::stm::jobs
