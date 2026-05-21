#include "environment_condition_monitor.hpp"
#include "zmq_subscriber.hpp"
#include "storage_handoff/storage_handoff.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <csignal>
#include <atomic>

static std::atomic<bool> g_running{true};

static void signal_handler(int /*sig*/) {
    std::cout << "\n[EC] Shutdown signal received.\n";
    g_running = false;
}

int main() {
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    std::cout << "═══════════════════════════════════════════════════════\n"
              << "  ORo Base — Environment Condition Module\n"
              << "═══════════════════════════════════════════════════════\n\n";

    // Load configuration
    std::string config_path = "../config/oro_base_edge_layer_config.json";
    if (const char* env_path = std::getenv("ORO_EDGE_CONFIG")) {
        config_path = env_path;
    }

    std::ifstream config_file(config_path);
    if (!config_file.is_open()) {
        // Try the alternative local fallback relative to build folder
        std::string fallback_path = "../../config/oro_base_edge_layer_config.json";
        config_file.open(fallback_path);
        if (config_file.is_open()) {
            config_path = fallback_path;
        } else {
            // Finally try the system-wide installation path
            fallback_path = "/etc/oro/oro_base_edge_layer_config.json";
            config_file.open(fallback_path);
            if (config_file.is_open()) {
                config_path = fallback_path;
            } else {
                std::cerr << "[EC] Error: Could not open config file from any path:\n"
                          << "  - ../config/oro_base_edge_layer_config.json\n"
                          << "  - ../../config/oro_base_edge_layer_config.json\n"
                          << "  - /etc/oro/oro_base_edge_layer_config.json\n";
                return 1;
            }
        }
    }

    std::cout << "[EC] Loaded configuration from: " << config_path << "\n";

    nlohmann::json config;
    try {
        config_file >> config;
    } catch (const nlohmann::json::parse_error& e) {
        std::cerr << "[EC] Error parsing config JSON: " << e.what() << "\n";
        return 1;
    }

    std::string device_id = "";
    std::string conn_str = "host=localhost user=oro_user password=ogmen dbname=oro_base_db";
    if (config.contains("global")) {
        device_id = config["global"].value("device_id", "");
        conn_str = config["global"].value("db_connection_string", conn_str);
    }

    uint64_t tick_interval_ms = 5000;
    std::string sensors_endpoint = "ipc:///tmp/oro_sensors.ipc";
    std::string location_zone = "zone_living_room";
    std::string sensor_source = "internal_sensor_hub";
    nlohmann::json comfort_thresholds = nlohmann::json::object();
    nlohmann::json light_estimator_cfg = nlohmann::json::object();

    if (config.contains("environment_condition_node")) {
        auto ec_cfg = config["environment_condition_node"];
        tick_interval_ms = ec_cfg.value("tick_interval_ms", 5000ULL);
        sensors_endpoint = ec_cfg.value("sensors_ipc_endpoint", "ipc:///tmp/oro_sensors.ipc");
        location_zone = ec_cfg.value("location_zone", "zone_living_room");
        sensor_source = ec_cfg.value("sensor_source", "internal_sensor_hub");
        if (ec_cfg.contains("comfort_thresholds")) {
            comfort_thresholds = ec_cfg["comfort_thresholds"];
        }
        if (ec_cfg.contains("light_estimator")) {
            light_estimator_cfg = ec_cfg["light_estimator"];
        }
    }

    if (device_id.empty()) {
        std::cerr << "[EC] Error: device_id must be provided in config.\n";
        return 1;
    }

    // Initialize Storage Handoff
    storage_handoff::StorageWriter storage_writer(conn_str);

    storage_writer.prepare("insert_signal",
        R"(
        INSERT INTO public.oro_base_signals (
            signal_id, device_id, dog_id, signal_type,
            signal_value_numeric, signal_value_text, signal_value_boolean,
            unit, observed_at, ingested_at, source, confidence, metadata, created_at
        )
        VALUES (
            $1, $2, $3, $4,
            $5, $6, $7,
            $8, $9, $10, $11, $12, $13::jsonb, NOW()
        )
        )");

    std::cout << "[EC] StorageWriter initialized.\n";

    // Initialize Monitor
    EnvironmentConditionMonitor monitor(storage_writer, device_id, location_zone, sensor_source, comfort_thresholds, tick_interval_ms, light_estimator_cfg);
    std::cout << "[EC] EnvironmentConditionMonitor initialized.\n";

    // Initialize ZMQ
    zmq::context_t context(1);
    ZmqSubscriber subscriber(monitor, context, sensors_endpoint);
    subscriber.start();

    // Main Loop
    std::cout << "[EC] Entering main tick loop...\n\n";

    while (g_running.load(std::memory_order_relaxed)) {
        uint64_t current_time = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());
        
        monitor.tick(current_time);
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    subscriber.stop();

    std::cout << "\n═══════════════════════════════════════════════════════\n"
              << "  Environment Condition Module shutdown complete.\n"
              << "═══════════════════════════════════════════════════════\n";
    return 0;
}
