#ifndef SCHEDULED_TASK_MANAGER_SCHEDULER_CONFIG_HPP
#define SCHEDULED_TASK_MANAGER_SCHEDULER_CONFIG_HPP

#include <nlohmann/json.hpp>
#include <string>

namespace oro::stm {

/**
 * @brief Loads and provides access to the scheduler's configuration.
 *
 * Configuration is read from the central oro_base_edge_layer_config.json file.
 * The scheduler-specific section lives under the "scheduled_task_manager" key.
 *
 * Current approach: config-driven (JSON file).
 * TODO: Migrate to database-driven configuration for dynamic job management.
 *       When migrating, the SchedulerConfig should query the scheduled_jobs
 *       table for job definitions, frequencies, and enabled states instead of
 *       reading from the JSON file. The JSON file will still serve as the
 *       fallback/seed configuration.
 */
class SchedulerConfig {
public:
  /**
   * @brief Load configuration from the given JSON file path.
   * @param config_path Absolute path to oro_base_edge_layer_config.json
   * @return true if loaded successfully, false otherwise.
   */
  bool load(const std::string &config_path);

  // ── Global accessors ──────────────────────────────────────
  std::string device_id() const;
  std::string db_connection_string() const;
  std::string timezone() const;

  // ── Scheduler-specific accessors ──────────────────────────
  int worker_threads() const;
  int tick_interval_ms() const;

  // ── Job overrides ─────────────────────────────────────────
  bool is_job_enabled(const std::string &job_name) const;
  int job_interval_seconds(const std::string &job_name,
                           int default_val) const;

  // ── Sub-config access for jobs ────────────────────────────
  nlohmann::json meal_schedules() const;
  nlohmann::json care_schedules() const;
  nlohmann::json comfort_thresholds() const;

  // ── Raw config access for job handlers ────────────────────
  const nlohmann::json &raw() const { return config_; }

private:
  nlohmann::json config_;
  nlohmann::json stm_config_; // "scheduled_task_manager" subtree
};

} // namespace oro::stm

#endif // SCHEDULED_TASK_MANAGER_SCHEDULER_CONFIG_HPP
