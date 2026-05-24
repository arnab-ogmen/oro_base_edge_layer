#ifndef ENVIRONMENT_CONDITION_MONITOR_HPP
#define ENVIRONMENT_CONDITION_MONITOR_HPP

#include "storage_handoff/storage_handoff.hpp"
#include "ambient_light_estimator.hpp"
#include <nlohmann/json.hpp>
#include <string>
#include <optional>
#include <mutex>
#include <memory>
    
struct EnvSignalRecord {
    int signal_id;
    std::string device_id;
    std::string signal_type;
    std::optional<double> signal_value_numeric;
    std::optional<std::string> signal_value_text;
    std::optional<bool> signal_value_boolean;
    std::string unit;
    uint64_t observed_at;
    uint64_t ingested_at;
    std::string source;
    std::optional<double> confidence;
    std::string metadata;
};

class EnvironmentConditionMonitor {
public:
    EnvironmentConditionMonitor(storage_handoff::StorageWriter& writer, 
                                const std::string& device_id, 
                                const std::string& location_zone,
                                const std::string& sensor_source,
                                const nlohmann::json& threshold_config,
                                uint64_t tick_interval_ms,
                                const nlohmann::json& light_estimator_config = nlohmann::json::object());
    ~EnvironmentConditionMonitor();

    // Write planned threshold signals to the database
    void emit_threshold_signals();

    // Update real-time measurements
    void update_temperature(double value_celsius, uint64_t current_time_ms);
    void update_humidity(double value_percentage, uint64_t current_time_ms);
    void update_light_level(double value_lux, uint64_t current_time_ms);

    void tick(uint64_t current_time_ms);

private:
    uint64_t now_ms();
    void emit_signal(const EnvSignalRecord& record);

    storage_handoff::StorageWriter& writer_;
    std::string device_id_;
    std::string location_zone_;
    std::string sensor_source_;
    nlohmann::json threshold_config_;
    
    bool thresholds_emitted_ = false;

    uint64_t tick_interval_ms_;
    uint64_t last_write_time_ms_ = 0;

    std::optional<double> latest_temp_;
    uint64_t temp_observed_at_ = 0;

    std::optional<double> latest_hum_;
    uint64_t hum_observed_at_ = 0;

    // Crossing state tracking variables
    bool temp_crossed_ = false;
    bool hum_crossed_ = false;

    std::mutex data_mutex_;

    // Production-grade Ambient Light Estimator pipeline
    std::unique_ptr<AmbientLightEstimator> light_estimator_;
    int target_light_signal_id_ = 47;
    std::string target_light_signal_type_ = "ambient_light_level";
    std::string target_light_unit_ = "lux";
};

#endif // ENVIRONMENT_CONDITION_MONITOR_HPP
