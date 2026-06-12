#include "scheduled_task_manager/job_executor.hpp"
#include "scheduled_task_manager/job_registry.hpp"
#include "scheduled_task_manager/lock_manager.hpp"
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
#include <nlohmann/json.hpp>

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

    // ── 1. Parse Command-Line Options ──
    std::string config_path = "/home/radxa/oro_base/oro_base_edge_layer/config/oro_base_edge_layer_config.json";
    std::string mode = "setup-cron"; // default mode
    std::string run_job_name = "";
    std::string run_date = "";     // optional: run daily job for a specific date (YYYY-MM-DD)

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--config" && i + 1 < argc) {
            config_path = argv[++i];
        } else if (arg == "--run" && i + 1 < argc) {
            mode = "run-job";
            run_job_name = argv[++i];
        } else if (arg == "--date" && i + 1 < argc) {
            run_date = argv[++i];  // e.g. 2026-06-11 — overrides 'today' in date-sensitive jobs
        } else if (arg == "--setup-cron") {
            mode = "setup-cron";
        } else if (arg == "-h" || arg == "--help") {
            std::cout << "Usage: " << argv[0] << " [options]\n"
                      << "Options:\n"
                      << "  --config <path>      Path to configuration JSON file\n"
                      << "  --run <job_name>     Execute a single job and exit\n"
                      << "  --date <YYYY-MM-DD>  Target date override for date-sensitive jobs\n"
                      << "  --setup-cron         Generate/update cron configuration and exit (default)\n"
                      << "  -h, --help           Show this help message\n";
            return 0;
        }
    }

    oro::stm::SchedulerConfig config;
    if (!config.load(config_path)) {
        std::cerr << "[STM] FATAL: Failed to load configuration.\n";
        return 1;
    }

    // ── 2. Initialize Database Writer ──
    std::string conn_str = config.db_connection_string();
    storage_handoff::StorageWriter writer(conn_str);
    std::cout << "[STM] Database writer initialized.\n";

    // ── 3. Generate Owner ID ──
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) != 0) {
        snprintf(hostname, sizeof(hostname), "unknown-host");
    }
    std::string owner_id = std::string(hostname) + "_" + std::to_string(getpid());
    std::cout << "[STM] Host owner ID: " << owner_id << "\n";

    // ── 4. Initialize Lock Manager & Executor ──
    oro::stm::LockManager lock_manager(writer, owner_id);
    oro::stm::JobExecutor executor(writer, lock_manager, owner_id);

    // ── 4a. Prepare SQL Statements ──
    lock_manager.prepare_statements();
    executor.prepare_statements();

    // TODO: Fetch user/dog identity dynamically (future scope)
    // Currently resolving user_id dynamically from user table by matching device_id
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

    oro::stm::jobs::prepare_care_job_statements(writer);
    oro::stm::jobs::prepare_device_job_statements(writer);
    oro::stm::jobs::prepare_health_job_statements(writer);
    oro::stm::jobs::prepare_summary_job_statements(writer);
    oro::stm::jobs::prepare_retry_job_statements(writer);
    oro::stm::jobs::prepare_cleanup_job_statements(writer);
    std::cout << "[STM] All job SQL statements prepared.\n";

    // ── 5. Build Job Registry ──
    oro::stm::JobRegistry registry;
    registry.initialize(config);

    // ── 6. Build Scheduler Engine (mainly for cron generation) ──
    oro::stm::SchedulerEngine engine(config, registry, executor);

    // ── 7. Execute according to parsed mode ──
    if (mode == "run-job") {
        std::cout << "[STM] Single job execution mode: '" << run_job_name << "'";
        if (!run_date.empty()) {
            std::cout << " (date override: " << run_date << ")";
        }
        std::cout << "\n";

        const auto* job_def = registry.find(run_job_name);
        if (!job_def) {
            std::cerr << "[STM] ERROR: Job '" << run_job_name << "' not found in registry.\n";
            return 1;
        }

        // Merge date override into a copy of the config before dispatching.
        // Jobs check config["run_date"] to override their computed 'today' window.
        nlohmann::json exec_config = config.raw_config();
        if (!run_date.empty()) {
            exec_config["run_date"] = run_date;
        }

        auto result = executor.execute(*job_def, exec_config);
        return result.success ? 0 : 1;
    } 
    
    // Default mode: setup-cron
    std::cout << "[STM] Setup cron mode.\n";
    std::string cron_file_path = "/etc/cron.d/oro_scheduled_tasks";
    std::string binary_path = "/usr/local/bin/scheduled_task_manager_node";
    
    bool ok = engine.generate_cron_config(cron_file_path, binary_path);
    if (!ok) {
        // Fallback to local directory if writing to /etc/cron.d fails (e.g. no root permission)
        cron_file_path = "/home/radxa/oro_base/oro_base_edge_layer/scheduled_task_manager/oro_cron";
        std::cout << "[STM] Attempting fallback to write cron file to " << cron_file_path << "...\n";
        ok = engine.generate_cron_config(cron_file_path, binary_path);
    }
    
    std::cout << "[STM] Shutdown complete.\n";
    return ok ? 0 : 1;
}
