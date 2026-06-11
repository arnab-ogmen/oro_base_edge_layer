#include "scheduled_task_manager/scheduler_config.hpp"
#include <fstream>
#include <iostream>

namespace oro::stm {

bool SchedulerConfig::load(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) {
        std::cerr << "[SchedulerConfig] Failed to open config file: " << path << "\n";
        return false;
    }

    try {
        f >> config_;
        if (config_.contains("scheduled_task_manager")) {
            stm_config_ = config_["scheduled_task_manager"];
        }
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[SchedulerConfig] Parse error: " << e.what() << "\n";
        return false;
    }
}

std::string SchedulerConfig::db_connection_string() const {
    if (config_.contains("global") && config_["global"].contains("db_connection_string")) {
        return config_["global"]["db_connection_string"];
    }
    return "host=localhost user=oro_user password=ogmen dbname=oro_base_db";
}

std::string SchedulerConfig::device_id() const {
    if (config_.contains("global") && config_["global"].contains("device_id")) {
        return config_["global"]["device_id"];
    }
    return "unknown-device";
}

int SchedulerConfig::worker_threads() const {
    if (stm_config_.contains("worker_threads")) {
        return stm_config_["worker_threads"].get<int>();
    }
    return 2;
}

int SchedulerConfig::tick_interval_ms() const {
    if (stm_config_.contains("tick_interval_ms")) {
        return stm_config_["tick_interval_ms"].get<int>();
    }
    return 1000;
}

bool SchedulerConfig::is_job_enabled(const std::string& job_name) const {
    if (stm_config_.contains("jobs") && stm_config_["jobs"].contains(job_name)) {
        const auto& job_cfg = stm_config_["jobs"][job_name];
        if (job_cfg.contains("enabled")) {
            return job_cfg["enabled"].get<bool>();
        }
    }
    return true;
}

int SchedulerConfig::job_interval_seconds(const std::string& job_name, int default_val) const {
    if (stm_config_.contains("jobs") && stm_config_["jobs"].contains(job_name)) {
        const auto& job_cfg = stm_config_["jobs"][job_name];
        if (job_cfg.contains("interval_seconds")) {
            return job_cfg["interval_seconds"].get<int>();
        }
    }
    return default_val;
}

} // namespace oro::stm
