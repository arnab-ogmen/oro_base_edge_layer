#include "scheduled_task_manager/scheduler_engine.hpp"
#include <iostream>
#include <fstream>

namespace oro::stm {

SchedulerEngine::SchedulerEngine(const SchedulerConfig& config, const JobRegistry& registry, JobExecutor& executor)
    : config_(config), registry_(registry), executor_(executor) {}

SchedulerEngine::~SchedulerEngine() {}

bool SchedulerEngine::generate_cron_config(const std::string& cron_file_path, const std::string& binary_path) {
    std::cout << "[SchedulerEngine] Generating cron configuration at " << cron_file_path << "...\n";
    
    std::ofstream cron_file(cron_file_path);
    if (!cron_file.is_open()) {
        std::cerr << "[SchedulerEngine] ERROR: Failed to open " << cron_file_path << " for writing.\n";
        return false;
    }

    cron_file << "# ORO Scheduled Task Manager Cron Jobs\n";
    cron_file << "# Generated dynamically by scheduled_task_manager_node\n";
    cron_file << "SHELL=/bin/bash\n";
    cron_file << "PATH=/usr/local/sbin:/usr/local/bin:/sbin:/bin:/usr/sbin:/usr/bin\n\n";

    for (const auto& job : registry_.jobs()) {
        if (!job.enabled) {
            cron_file << "# Job '" << job.name << "' is disabled in configuration\n";
            continue;
        }

        // Convert interval_seconds to cron pattern (minimum granularity is 1 minute)
        std::string cron_pattern = "";
        int minutes = job.interval_seconds / 60;
        if (minutes < 1) {
            minutes = 1; // Cron minimum granularity is 1 minute
        }

        if (job.interval_seconds == 60) {
            cron_pattern = "* * * * *";
        } else if (job.interval_seconds == 3600) {
            cron_pattern = "0 * * * *";
        } else if (job.interval_seconds == 86400) {
            cron_pattern = "0 0 * * *";
        } else if (job.interval_seconds == 604800) {
            cron_pattern = "0 0 * * 0";
        } else {
            cron_pattern = "*/" + std::to_string(minutes) + " * * * *";
        }

        cron_file << "# " << job.display_name << " (Interval: " << job.interval_seconds << "s, Priority: " << priority_to_string(job.priority) << ")\n";
        cron_file << cron_pattern << " root " << binary_path << " --run " << job.name << "\n\n";
    }

    cron_file.close();
    std::cout << "[SchedulerEngine] Cron configuration generated successfully.\n";
    return true;
}

} // namespace oro::stm
