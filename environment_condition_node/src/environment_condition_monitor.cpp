#include "environment_condition_monitor.hpp"
#include <chrono>
#include <iostream>
#include <nlohmann/json.hpp>

EnvironmentConditionMonitor::EnvironmentConditionMonitor(storage_handoff::StorageWriter& writer, 
                                                         const std::string& device_id, 
                                                         const std::string& location_zone,
                                                         const std::string& sensor_source,
                                                         const nlohmann::json& threshold_config,
                                                         uint64_t tick_interval_ms,
                                                         const nlohmann::json& light_estimator_config)
    : writer_(writer), device_id_(device_id), location_zone_(location_zone), 
      sensor_source_(sensor_source), threshold_config_(threshold_config),
      tick_interval_ms_(tick_interval_ms) {
    
    // Parse light estimator parameters with robust defaults
    std::string device_path = light_estimator_config.value("device_path", "/dev/video13");
    double model_a = light_estimator_config.value("model_a", 0.05);
    double model_b = light_estimator_config.value("model_b", 1.8);
    double ema_alpha = light_estimator_config.value("ema_alpha", 0.15);
    int percentile = light_estimator_config.value("percentile", 50);

    // Support configurable mapping to cover potential target Signal ID / Signal Type variations
    target_light_signal_id_ = light_estimator_config.value("target_signal_id", 47);
    target_light_signal_type_ = light_estimator_config.value("target_signal_type", "ambient_light_level");
    target_light_unit_ = light_estimator_config.value("target_unit", "lux");

    light_estimator_ = std::make_unique<AmbientLightEstimator>(
        device_path, model_a, model_b, ema_alpha, percentile
    );
    light_estimator_->start();
}

EnvironmentConditionMonitor::~EnvironmentConditionMonitor() {
    if (light_estimator_) {
        light_estimator_->stop();
    }
}

uint64_t EnvironmentConditionMonitor::now_ms() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
}

void EnvironmentConditionMonitor::emit_signal(const EnvSignalRecord& record) {
    std::string obs_at = storage_handoff::StorageWriter::unix_ms_to_iso8601(record.observed_at);
    std::string ing_at = storage_handoff::StorageWriter::unix_ms_to_iso8601(record.ingested_at);
    
    std::optional<std::string> boolean_opt;
    if (record.signal_value_boolean.has_value()) {
        boolean_opt = *record.signal_value_boolean ? "true" : "false";
    }

    std::optional<std::string> dog_id_opt = std::nullopt; // Environmental signals do not have a dog_id

    writer_.execute_prepared("insert_signal", record.signal_id, record.device_id, dog_id_opt,
                             record.signal_type, record.signal_value_numeric,
                             record.signal_value_text, boolean_opt, record.unit,
                             obs_at, ing_at, record.source, record.confidence,
                             record.metadata);
}

void EnvironmentConditionMonitor::emit_threshold_signals() {
    if (thresholds_emitted_) return;
    
    std::cout << "[EC] Emitting planned configuration threshold signals to database...\n";
    
    std::string configured_by = threshold_config_.value("configured_by", "N/A");
    std::string updated_at = threshold_config_.value("updated_at", "");
    if (updated_at.empty()) {
        updated_at = storage_handoff::StorageWriter::unix_ms_to_iso8601(now_ms());
    }

    uint64_t current_time = now_ms();

    // 1. #111 environment_comfort_range_threshold
    double temp_min = 18.0;
    double temp_max = 26.0;
    if (threshold_config_.contains("environment_comfort_range")) {
        temp_min = threshold_config_["environment_comfort_range"].value("min", 18.0);
        temp_max = threshold_config_["environment_comfort_range"].value("max", 26.0);
    }
    
    std::string temp_range_str = "[" + std::to_string(temp_min) + ", " + std::to_string(temp_max) + "]";
    nlohmann::json temp_meta = {
        {"zone_id", location_zone_},
        {"configured_by", configured_by},
        {"updated_at", updated_at},
        {"range_min", temp_min},
        {"range_max", temp_max}
    };

    EnvSignalRecord rec_temp_thresh;
    rec_temp_thresh.signal_id = 111;
    rec_temp_thresh.device_id = device_id_;
    rec_temp_thresh.signal_type = "environment_comfort_range_threshold";
    rec_temp_thresh.signal_value_numeric = std::nullopt;
    rec_temp_thresh.signal_value_text = temp_range_str;
    rec_temp_thresh.unit = "degrees Celsius";
    rec_temp_thresh.observed_at = current_time;
    rec_temp_thresh.ingested_at = current_time;
    rec_temp_thresh.source = "EC";
    rec_temp_thresh.confidence = 1.0;
    rec_temp_thresh.metadata = temp_meta.dump();
    emit_signal(rec_temp_thresh);
    std::cout << "[EC] Saved environment_comfort_range_threshold: " << temp_range_str << "\n";

    // 2. #112 humidity_comfort_range_threshold
    double hum_min = 30.0;
    double hum_max = 70.0;
    if (threshold_config_.contains("humidity_comfort_range")) {
        hum_min = threshold_config_["humidity_comfort_range"].value("min", 30.0);
        hum_max = threshold_config_["humidity_comfort_range"].value("max", 70.0);
    }
    
    std::string hum_range_str = "[" + std::to_string(hum_min) + ", " + std::to_string(hum_max) + "]";
    nlohmann::json hum_meta = {
        {"zone_id", location_zone_},
        {"configured_by", configured_by},
        {"updated_at", updated_at},
        {"range_min", hum_min},
        {"range_max", hum_max}
    };

    EnvSignalRecord rec_hum_thresh;
    rec_hum_thresh.signal_id = 112;
    rec_hum_thresh.device_id = device_id_;
    rec_hum_thresh.signal_type = "humidity_comfort_range_threshold";
    rec_hum_thresh.signal_value_numeric = std::nullopt;
    rec_hum_thresh.signal_value_text = hum_range_str;
    rec_hum_thresh.unit = "percentage";
    rec_hum_thresh.observed_at = current_time;
    rec_hum_thresh.ingested_at = current_time;
    rec_hum_thresh.source = "EC";
    rec_hum_thresh.confidence = 1.0;
    rec_hum_thresh.metadata = hum_meta.dump();
    emit_signal(rec_hum_thresh);
    std::cout << "[EC] Saved humidity_comfort_range_threshold: " << hum_range_str << "\n";

    // 3. #113 light_threshold
    double light_thresh = threshold_config_.value("light_threshold", 200.0);
    nlohmann::json light_meta = {
        {"zone_id", location_zone_},
        {"configured_by", configured_by},
        {"updated_at", updated_at}
    };

    EnvSignalRecord rec_light_thresh;
    rec_light_thresh.signal_id = 113;
    rec_light_thresh.device_id = device_id_;
    rec_light_thresh.signal_type = "light_threshold";
    rec_light_thresh.signal_value_numeric = light_thresh;
    rec_light_thresh.signal_value_text = std::nullopt;
    rec_light_thresh.unit = "lux";
    rec_light_thresh.observed_at = current_time;
    rec_light_thresh.ingested_at = current_time;
    rec_light_thresh.source = "EC";
    rec_light_thresh.confidence = 1.0;
    rec_light_thresh.metadata = light_meta.dump();
    emit_signal(rec_light_thresh);
    std::cout << "[EC] Saved light_threshold: " << light_thresh << " lux\n";

    thresholds_emitted_ = true;
}

void EnvironmentConditionMonitor::update_temperature(double value_celsius, uint64_t current_time_ms) {
    std::lock_guard<std::mutex> lock(data_mutex_);
    latest_temp_ = value_celsius;
    temp_observed_at_ = current_time_ms;

    // Check comfort thresholds crossing dynamically
    double temp_min = 18.0;
    double temp_max = 26.0;
    if (threshold_config_.contains("environment_comfort_range")) {
        temp_min = threshold_config_["environment_comfort_range"].value("min", 18.0);
        temp_max = threshold_config_["environment_comfort_range"].value("max", 26.0);
    }

    bool is_outside = (value_celsius < temp_min || value_celsius > temp_max);
    if (is_outside) {
        if (!temp_crossed_) {
            temp_crossed_ = true;
            
            std::string configured_by = threshold_config_.value("configured_by", "system");
            std::string updated_at = threshold_config_.value("updated_at", "");
            if (updated_at.empty()) {
                updated_at = storage_handoff::StorageWriter::unix_ms_to_iso8601(current_time_ms);
            }

            nlohmann::json temp_meta = {
                {"zone_id", location_zone_},
                {"configured_by", configured_by},
                {"updated_at", updated_at}
            };

            EnvSignalRecord rec_temp_thresh;
            rec_temp_thresh.signal_id = 111;
            rec_temp_thresh.device_id = device_id_;
            rec_temp_thresh.signal_type = "environment_comfort_range_threshold";
            rec_temp_thresh.signal_value_numeric = value_celsius;
            rec_temp_thresh.signal_value_text = std::nullopt;
            rec_temp_thresh.unit = "degrees Celsius";
            rec_temp_thresh.observed_at = current_time_ms;
            rec_temp_thresh.ingested_at = now_ms();
            rec_temp_thresh.source = "EC";
            rec_temp_thresh.confidence = 1.0;
            rec_temp_thresh.metadata = temp_meta.dump();
            emit_signal(rec_temp_thresh);

            std::cout << "[EC] [CROSS] Environment temperature " << value_celsius 
                      << " C crossed comfort threshold range " << temp_min << " - " << temp_max 
                      << ". Threshold signal logged to DB.\n";
        }
    } else {
        if (temp_crossed_) {
            temp_crossed_ = false;
            std::cout << "[EC] Environment temperature " << value_celsius 
                      << " C returned inside comfortable range [" << temp_min << ", " << temp_max << "].\n";
        }
    }
}

void EnvironmentConditionMonitor::update_humidity(double value_percentage, uint64_t current_time_ms) {
    std::lock_guard<std::mutex> lock(data_mutex_);
    latest_hum_ = value_percentage;
    hum_observed_at_ = current_time_ms;

    // Check comfort thresholds crossing dynamically
    double hum_min = 30.0;
    double hum_max = 70.0;
    if (threshold_config_.contains("humidity_comfort_range")) {
        hum_min = threshold_config_["humidity_comfort_range"].value("min", 30.0);
        hum_max = threshold_config_["humidity_comfort_range"].value("max", 70.0);
    }

    bool is_outside = (value_percentage < hum_min || value_percentage > hum_max);
    if (is_outside) {
        if (!hum_crossed_) {
            hum_crossed_ = true;
            
            std::string configured_by = threshold_config_.value("configured_by", "system");
            std::string updated_at = threshold_config_.value("updated_at", "");
            if (updated_at.empty()) {
                updated_at = storage_handoff::StorageWriter::unix_ms_to_iso8601(current_time_ms);
            }

            nlohmann::json hum_meta = {
                {"zone_id", location_zone_},
                {"configured_by", configured_by},
                {"updated_at", updated_at}
            };

            EnvSignalRecord rec_hum_thresh;
            rec_hum_thresh.signal_id = 112;
            rec_hum_thresh.device_id = device_id_;
            rec_hum_thresh.signal_type = "humidity_comfort_range_threshold";
            rec_hum_thresh.signal_value_numeric = value_percentage;
            rec_hum_thresh.signal_value_text = std::nullopt;
            rec_hum_thresh.unit = "percentage";
            rec_hum_thresh.observed_at = current_time_ms;
            rec_hum_thresh.ingested_at = now_ms();
            rec_hum_thresh.source = "EC";
            rec_hum_thresh.confidence = 1.0;
            rec_hum_thresh.metadata = hum_meta.dump();
            emit_signal(rec_hum_thresh);

            std::cout << "[EC] [CROSS] Ambient humidity " << value_percentage 
                      << " % crossed comfort threshold range " << hum_min << " - " << hum_max 
                      << ". Threshold signal logged to DB.\n";
        }
    } else {
        if (hum_crossed_) {
            hum_crossed_ = false;
            std::cout << "[EC] Ambient humidity " << value_percentage 
                      << " % returned inside comfortable range [" << hum_min << ", " << hum_max << "].\n";
        }
    }
}

void EnvironmentConditionMonitor::update_light_level(double value_lux, uint64_t current_time_ms) {
    std::string obs_iso = storage_handoff::StorageWriter::unix_ms_to_iso8601(current_time_ms);
    nlohmann::json meta = {
        {"event_time", obs_iso},
        {"sensor_source", sensor_source_},
        {"location_zone", location_zone_}
    };

    EnvSignalRecord rec;
    rec.signal_id = 47;
    rec.device_id = device_id_;
    rec.signal_type = "ambient_light_level";
    rec.signal_value_numeric = value_lux;
    rec.signal_value_text = std::nullopt;
    rec.unit = "lux";
    rec.observed_at = current_time_ms;
    rec.ingested_at = now_ms();
    rec.source = "EC";
    rec.confidence = 1.0;
    rec.metadata = meta.dump();

    emit_signal(rec);
    // std::cout << "[EC] Recorded ambient_light_level: " << value_lux << " lux\n";
}

void EnvironmentConditionMonitor::tick(uint64_t current_time_ms) {
    // Write threshold configurations if they haven't been written yet
    // emit_threshold_signals();
    
    if (last_write_time_ms_ == 0 || current_time_ms >= last_write_time_ms_ + tick_interval_ms_) {
        std::optional<double> temp_to_write;
        uint64_t temp_obs = 0;
        std::optional<double> hum_to_write;
        uint64_t hum_obs = 0;
        
        {
            std::lock_guard<std::mutex> lock(data_mutex_);
            temp_to_write = latest_temp_;
            temp_obs = temp_observed_at_;
            hum_to_write = latest_hum_;
            hum_obs = hum_observed_at_;
        }
        
        bool wrote_any = false;
        if (temp_to_write.has_value()) {
            std::string obs_iso = storage_handoff::StorageWriter::unix_ms_to_iso8601(temp_obs);
            nlohmann::json meta = {
                {"event_time", obs_iso},
                {"sensor_source", sensor_source_},
                {"location_zone", location_zone_}
            };

            EnvSignalRecord rec;
            rec.signal_id = 46;
            rec.device_id = device_id_;
            rec.signal_type = "environment_temperature";
            rec.signal_value_numeric = *temp_to_write;
            rec.signal_value_text = std::nullopt;
            rec.unit = "degrees Celsius";
            rec.observed_at = temp_obs;
            rec.ingested_at = now_ms();
            rec.source = "EC";
            rec.confidence = 1.0;
            rec.metadata = meta.dump();

            emit_signal(rec);
            std::cout << "[EC] Recorded environment_temperature: " << *temp_to_write << " C\n";
            wrote_any = true;
        }

        if (hum_to_write.has_value()) {
            std::string obs_iso = storage_handoff::StorageWriter::unix_ms_to_iso8601(hum_obs);
            nlohmann::json meta = {
                {"event_time", obs_iso},
                {"sensor_source", sensor_source_},
                {"location_zone", location_zone_}
            };

            EnvSignalRecord rec;
            rec.signal_id = 48;
            rec.device_id = device_id_;
            rec.signal_type = "ambient_humidity";
            rec.signal_value_numeric = *hum_to_write;
            rec.signal_value_text = std::nullopt;
            rec.unit = "percentage";
            rec.observed_at = hum_obs;
            rec.ingested_at = now_ms();
            rec.source = "EC";
            rec.confidence = 1.0;
            rec.metadata = meta.dump();

            emit_signal(rec);
            std::cout << "[EC] Recorded ambient_humidity: " << *hum_to_write << " %\n";
            wrote_any = true;
        }

        if (light_estimator_) {
            double lux = 0.0;
            double confidence = 0.0;
            std::string bucket = "";
            if (light_estimator_->get_latest_lux(lux, confidence, bucket)) {
                std::string obs_iso = storage_handoff::StorageWriter::unix_ms_to_iso8601(current_time_ms);
                nlohmann::json meta = {
                    {"event_time", obs_iso},
                    {"sensor_source", "zmq_video_socket_flatbuffer"},
                    {"location_zone", location_zone_},
                    {"lux_bucket", bucket}
                };

                EnvSignalRecord rec;
                rec.signal_id = target_light_signal_id_;
                rec.device_id = device_id_;
                rec.signal_type = target_light_signal_type_;
                rec.signal_value_numeric = lux;
                rec.signal_value_text = bucket;
                rec.unit = target_light_unit_;
                rec.observed_at = current_time_ms;
                rec.ingested_at = now_ms();
                rec.source = "EC";
                rec.confidence = confidence;
                rec.metadata = meta.dump();

                emit_signal(rec);
                std::cout << "[EC] Recorded " << target_light_signal_type_ << ": " << lux 
                          << " (" << target_light_unit_ << "), bucket: " << bucket 
                          << ", confidence: " << confidence << "\n";

                double light_thresh = threshold_config_.value("light_threshold", 50.0);
                if (lux < light_thresh) {
                    EnvSignalRecord cover_rec;
                    cover_rec.signal_id = 113;
                    cover_rec.device_id = device_id_;
                    cover_rec.signal_type = "light_threshold";
                    cover_rec.signal_value_numeric = lux;
                    cover_rec.signal_value_text = bucket;
                    cover_rec.unit = "lux";
                    cover_rec.observed_at = current_time_ms;
                    cover_rec.ingested_at = now_ms();
                    cover_rec.source = "EC";
                    cover_rec.confidence = confidence;
                    cover_rec.metadata = meta.dump();

                    emit_signal(cover_rec);
                    std::cout << "[EC] Recorded light_threshold: " << lux << " lux\n";
                }
                wrote_any = true;   
            }
        }

        if (wrote_any) {
            last_write_time_ms_ = current_time_ms;
        }
    }
}
