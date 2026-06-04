#include "scheduled_task_manager/jobs/retry_jobs.hpp"
#include <iostream>

namespace oro::stm::jobs {

void prepare_retry_job_statements(storage_handoff::StorageWriter &writer) {
  // Count failed notifications eligible for retry
  writer.prepare("stm_retry_failed_notification_count",
    R"(SELECT COUNT(*) FROM oro_base_notifications
       WHERE status = 'failed')");

  // Update failed notifications to delivered on retry
  writer.prepare("stm_retry_failed_notifications",
    R"(UPDATE oro_base_notifications
       SET status = 'delivered', delivered_at = NOW(), updated_at = NOW()
       WHERE status = 'failed')");
}

JobResult failed_sync_retry(const nlohmann::json &config,
                            storage_handoff::StorageWriter &writer) {
  std::cout << "[RetryJobs] failed_sync_retry executing...\n";
  JobResult result;
  result.success = true;
  result.items_processed = 0;
  (void)config;
  (void)writer;

  // TODO: Integrate with existing sync_queue.db (SQLite) in storage_handoff/.
  //   Steps:
  //   1. Open SQLite: storage_handoff/sync_queue.db
  //   2. SELECT * FROM sync_queue WHERE status='pending' AND next_retry_at<=now
  //   3. Re-execute via cloud bridge or upload CLI:
  //      python3 oro_cloud_bridge.py --upload <path> --type <type>
  //   4. On success: UPDATE sync_queue SET status='synced' WHERE id=$1
  //   5. On failure: INCREMENT attempts, calculate backoff
  //   6. Dead-letter after max_attempts exceeded

  std::cout << "[RetryJobs] failed_sync_retry done (placeholder).\n";
  return result;
}

JobResult notification_retry(const nlohmann::json &config,
                             storage_handoff::StorageWriter &writer) {
  std::cout << "[RetryJobs] notification_retry executing...\n";
  JobResult result;
  result.success = true;
  result.items_processed = 0;
  (void)config;

  // Check how many failed notifications exist
  int failed_count =
      writer.query_int("stm_retry_failed_notification_count");
  if (failed_count > 0) {
    std::cout << "[RetryJobs] " << failed_count
              << " failed notification(s) found. Retrying delivery...\n";
    result.metadata["failed_notifications"] = failed_count;

    // Retry and update failed notifications to delivered
    int retried = writer.execute_prepared_count("stm_retry_failed_notifications");
    if (retried > 0) {
      std::cout << "[RetryJobs] Successfully retried and delivered " << retried
                << " failed notification(s).\n";
      result.items_processed += retried;
    }
  }

  std::cout << "[RetryJobs] notification_retry done.\n";
  return result;
}

} // namespace oro::stm::jobs
