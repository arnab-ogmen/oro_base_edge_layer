#include "scheduled_task_manager/scheduler_config.hpp"
#include <fstream>
#include <iostream>

namespace oro::stm {

bool SchedulerConfig::load(const std::string &config_path) {
  try {
    std::ifstream file(config_path);
    if (!file.is_open()) {
      std::cerr << "[SchedulerConfig] Failed to open config file: "
                << config_path << "\n";
      return false;
    }

    config_ = nlohmann::json::parse(file);

    if (config_.contains("scheduled_task_manager")) {
      stm_config_ = config_["scheduled_task_manager"];
    } else {
      std::cout << "[SchedulerConfig] Warning: No 'scheduled_task_manager' "
                   "section in config. Using defaults.\n";
      stm_config_ = nlohmann::json::object();
    }

    std::cout << "[SchedulerConfig] Loaded configuration from " << config_path
              << "\n";
    return true;
  } catch (const std::exception &e) {
    std::cerr << "[SchedulerConfig] Error parsing config: " << e.what()
              << "\n";
    return false;
  }
}

std::string SchedulerConfig::device_id() const {
  return config_.value("/global/device_id"_json_pointer,
                       std::string("00000000-0000-0000-0000-000000000000"));
}

std::string SchedulerConfig::db_connection_string() const {
  return config_.value(
      "/global/db_connection_string"_json_pointer,
      std::string(
          "host=localhost user=oro_user password=ogmen dbname=oro_base_db"));
}

std::string SchedulerConfig::timezone() const {
  // Currently config-driven.
  // TODO: Read timezone from database (per-user) when DB-driven migration
  //       is complete. Query: SELECT timezone FROM oro_base_user WHERE ...
  //       For now, fallback chain: stm_config -> global -> "UTC"
  if (stm_config_.contains("timezone")) {
    return stm_config_["timezone"].get<std::string>();
  }
  return config_.value("/global/timezone"_json_pointer, std::string("UTC"));
}

int SchedulerConfig::worker_threads() const {
  return stm_config_.value("worker_threads", 2);
}

int SchedulerConfig::tick_interval_ms() const {
  return stm_config_.value("tick_interval_ms", 1000);
}

bool SchedulerConfig::is_job_enabled(const std::string &job_name) const {
  if (stm_config_.contains("jobs") && stm_config_["jobs"].contains(job_name) &&
      stm_config_["jobs"][job_name].contains("enabled")) {
    return stm_config_["jobs"][job_name]["enabled"].get<bool>();
  }
  return true; // Jobs are enabled by default
}

int SchedulerConfig::job_interval_seconds(const std::string &job_name,
                                          int default_val) const {
  if (stm_config_.contains("jobs") && stm_config_["jobs"].contains(job_name) &&
      stm_config_["jobs"][job_name].contains("interval_seconds")) {
    return stm_config_["jobs"][job_name]["interval_seconds"].get<int>();
  }
  return default_val;
}

nlohmann::json SchedulerConfig::meal_schedules() const {
  if (config_.contains("meal_schedules")) {
    return config_["meal_schedules"];
  }
  return nlohmann::json::object();
}

nlohmann::json SchedulerConfig::care_schedules() const {
  // Currently reads from config JSON.
  // TODO: Query oro_base_care_schedules table for DB-driven care schedules.
  //       Placeholder query:
  //         SELECT * FROM oro_base_care_schedules
  //         WHERE device_id = $1 AND is_active = true
  //       Merge config-based and DB-based schedules when migrated.
  if (stm_config_.contains("care_schedules")) {
    return stm_config_["care_schedules"];
  }
  return nlohmann::json::array();
}

nlohmann::json SchedulerConfig::comfort_thresholds() const {
  if (config_.contains("environment_condition_node") &&
      config_["environment_condition_node"].contains("comfort_thresholds")) {
    return config_["environment_condition_node"]["comfort_thresholds"];
  }
  return nlohmann::json::object();
}

} // namespace oro::stm
