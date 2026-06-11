#ifndef SCHEDULED_TASK_MANAGER_SCHEDULER_CONFIG_HPP
#define SCHEDULED_TASK_MANAGER_SCHEDULER_CONFIG_HPP

#include <string>
#include <nlohmann/json.hpp>

namespace oro::stm {

class SchedulerConfig {
public:
    SchedulerConfig() = default;
    ~SchedulerConfig() = default;

    /**
     * @brief Load configuration from the JSON file.
     * @return true if loaded successfully, false otherwise.
     */
    bool load(const std::string& path);

    std::string db_connection_string() const;
    std::string device_id() const;
    int worker_threads() const;
    int tick_interval_ms() const;

    bool is_job_enabled(const std::string& job_name) const;
    int job_interval_seconds(const std::string& job_name, int default_val) const;

    const nlohmann::json& raw_config() const { return config_; }
    const nlohmann::json& stm_config() const { return stm_config_; }

private:
    nlohmann::json config_;
    nlohmann::json stm_config_;
};

} // namespace oro::stm

#endif // SCHEDULED_TASK_MANAGER_SCHEDULER_CONFIG_HPP
