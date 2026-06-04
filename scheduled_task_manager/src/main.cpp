#include "scheduled_task_manager/job_executor.hpp"
#include "scheduled_task_manager/job_registry.hpp"
#include "scheduled_task_manager/scheduler_config.hpp"
#include "scheduled_task_manager/scheduler_engine.hpp"
#include "scheduled_task_manager/jobs/care_jobs.hpp"
#include "scheduled_task_manager/jobs/cleanup_jobs.hpp"
#include "scheduled_task_manager/jobs/device_jobs.hpp"
#include "scheduled_task_manager/jobs/health_jobs.hpp"
#include "scheduled_task_manager/jobs/retry_jobs.hpp"
#include "scheduled_task_manager/jobs/summary_jobs.hpp"
#include "storage_handoff/storage_handoff.hpp"
#include <atomic>
#include <csignal>
#include <iostream>
#include <string>
#include <unistd.h>
#include <zmq.hpp>

std::atomic<bool> g_running{true};

void signal_handler(int) {
  std::cout << "\n[STM] Terminating service...\n";
  g_running = false;
}

int main(int argc, char *argv[]) {
  std::signal(SIGINT, signal_handler);
  std::signal(SIGTERM, signal_handler);

  std::cout << "[STM] ======================================\n";
  std::cout << "[STM] Scheduled Task Manager — Starting...\n";
  std::cout << "[STM] ======================================\n";

  // ── 1. Load Configuration ──────────────────────────────────
  std::string config_path =
      "/home/radxa/oro_base/oro_base_edge_layer/config/"
      "oro_base_edge_layer_config.json";
  if (argc > 1) {
    config_path = argv[1];
  }

  oro::stm::SchedulerConfig config;
  if (!config.load(config_path)) {
    std::cerr << "[STM] FATAL: Failed to load configuration.\n";
    return 1;
  }

  // ── 2. Initialize Database Writer ──────────────────────────
  std::string conn_str = config.db_connection_string();
  storage_handoff::StorageWriter writer(conn_str);
  std::cout << "[STM] Database writer initialized.\n";

  // ── 3. Generate Owner ID ─────────────────────────
  char hostname[256];
  gethostname(hostname, sizeof(hostname));
  std::string owner_id =
      std::string(hostname) + "_" + std::to_string(getpid());
  std::cout << "[STM] Host owner ID: " << owner_id << "\n";

  // ── 4. Initialize Core Components ─────────────────────────
  zmq::context_t zmq_context(1);

  oro::stm::JobExecutor executor(writer, zmq_context);

  // ── 4a. Prepare Generic Notification Statements ────────────
  writer.prepare("stm_emit_notification",
    R"(INSERT INTO oro_base_notifications (
         device_id, dog_id, user_id, notification_type, category, notification_key,
         title, message, priority, status, delivery_channel, trigger_mode,
         scheduled_for, generated_at, payload, dedupe_key, expires_at
       )
       SELECT 
         $1::uuid, 
         COALESCE($2::uuid, dev.dog_id), 
         u.user_id, 
         $3, 
         $4, 
         $5, 
         $6, 
         $7, 
         $8, 
         'pending', 
         'in_app', 
         'scheduled', 
         NOW(), 
         NOW(), 
         $9::jsonb, 
         $10, 
         NOW() + INTERVAL '24 hours'
       FROM oro_base_user u
       LEFT JOIN oro_base_device dev ON dev.device_id = $1::uuid
       WHERE u.device_id = $1::uuid
         AND NOT EXISTS (
           SELECT 1 FROM oro_base_notifications 
           WHERE dedupe_key = $10
         )
       LIMIT 1)");

  // ── 4b. Prepare Job-Specific SQL Statements ────────────────
  oro::stm::jobs::prepare_care_job_statements(writer);
  oro::stm::jobs::prepare_device_job_statements(writer);
  oro::stm::jobs::prepare_health_job_statements(writer);
  oro::stm::jobs::prepare_summary_job_statements(writer);
  oro::stm::jobs::prepare_retry_job_statements(writer);
  oro::stm::jobs::prepare_cleanup_job_statements(writer);
  std::cout << "[STM] All job SQL statements prepared.\n";

  // ── 5. Build Job Registry ─────────────────────────────────
  oro::stm::JobRegistry registry;
  registry.initialize(config, executor);

  // ── 6. Start Scheduler Engine ──────────────────────────────
  oro::stm::SchedulerEngine engine(config, registry, executor);
  engine.start();

  std::cout << "[STM] Service running. Press Ctrl+C to stop.\n";

  // ── 7. Main Loop (keep alive until signal) ─────────────────
  while (g_running.load()) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }

  // ── 8. Graceful Shutdown ───────────────────────────────────
  engine.stop();
  std::cout << "[STM] Service shutdown complete.\n";
  return 0;
}
