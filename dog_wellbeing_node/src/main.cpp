#include "dog_wellbeing_monitor.hpp"
#include "zmq_subscriber.hpp"
#include "storage_handoff/storage_handoff.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <csignal>
#include <atomic>

static std::atomic<bool> g_running{true};

static void signal_handler(int /*sig*/) {
    std::cout << "\n[DW] Shutdown signal received.\n";
    g_running = false;
}

int main() {
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    std::cout << "═══════════════════════════════════════════════════════\n"
              << "  ORo Base — Dog Wellbeing Module\n"
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
                std::cerr << "[DW] Error: Could not open config file from any path:\n"
                          << "  - ../config/oro_base_edge_layer_config.json\n"
                          << "  - ../../config/oro_base_edge_layer_config.json\n"
                          << "  - /etc/oro/oro_base_edge_layer_config.json\n";
                return 1;
            }
        }
    }

    std::cout << "[DW] Loaded configuration from: " << config_path << "\n";

    nlohmann::json config;
    try {
        config_file >> config;
    } catch (const nlohmann::json::parse_error& e) {
        std::cerr << "[DW] Error parsing config JSON: " << e.what() << "\n";
        return 1;
    }

    std::string device_id = "";
    std::string conn_str = "host=localhost user=oro_user password=ogmen dbname=oro_base_db";
    if (config.contains("global")) {
        device_id = config["global"].value("device_id", "");
        conn_str = config["global"].value("db_connection_string", conn_str);
    }

    std::string dog_id = "";
    uint64_t dummy_interval_ms = 300000;
    std::string sensors_endpoint = "ipc:///tmp/oro_sensors.ipc";
    
    if (config.contains("dog_wellbeing_node")) {
        dog_id = config["dog_wellbeing_node"].value("dog_id", "");
        dummy_interval_ms = config["dog_wellbeing_node"].value("dummy_signal_interval_ms", 300000);
        sensors_endpoint = config["dog_wellbeing_node"].value("sensors_ipc_endpoint", "ipc:///tmp/oro_sensors.ipc");
    }

    if (device_id.empty()) {
        std::cerr << "[DW] Error: device_id must be provided in config.\n";
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

    storage_writer.prepare("get_daily_food_intake",
        R"(
        SELECT COALESCE(SUM(sub.val), 0.0) FROM (
            SELECT DISTINCT ON (metadata->>'meal_id') signal_value_numeric AS val
            FROM public.oro_base_signals
            WHERE signal_type = 'food_intake_per_meal' 
              AND dog_id = $1 
              AND observed_at >= $2::timestamptz
              AND metadata->>'meal_id' <> $3
            ORDER BY metadata->>'meal_id', observed_at DESC
        ) sub
        )");

    std::cout << "[DW] StorageWriter initialized.\n";

    std::vector<MealSchedule> meal_schedules;
    if (config.contains("meal_schedules") && config["meal_schedules"].is_object()) {
        for (auto& [key, value] : config["meal_schedules"].items()) {
            if (value.is_object()) {
                MealSchedule sched;
                sched.name = key;
                sched.intended_quantity_grams = value.value("intended_quantity_grams", 0.0);
                sched.start_time = value.value("start_time", "");
                sched.end_time = value.value("end_time", "");
                sched.meal_id = value.value("meal_id", "");
                sched.bowl_id = value.value("bowl_id", "");
                meal_schedules.push_back(sched);
            }
        }
    }

    std::cout << "[DW] Loaded " << meal_schedules.size() << " meal schedules:\n";
    for (const auto& sched : meal_schedules) {
        std::cout << "  - " << sched.name << ": " << sched.start_time << " - " << sched.end_time 
                  << ", meal_id: " << sched.meal_id << ", bowl_id: '" << sched.bowl_id << "'\n";
    }

    // Initialize Monitor
    DogWellbeingMonitor monitor(storage_writer, device_id, dog_id, dummy_interval_ms, meal_schedules);
    std::cout << "[DW] Monitor initialized.\n";

    // Initialize ZMQ
    zmq::context_t context(1);
    ZmqSubscriber subscriber(monitor, context, sensors_endpoint);
    subscriber.start();

    // Main Loop
    std::cout << "[DW] Entering main tick loop...\n\n";

    while (g_running.load(std::memory_order_relaxed)) {
        uint64_t current_time = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());
        
        monitor.tick(current_time);
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    subscriber.stop();

    std::cout << "\n═══════════════════════════════════════════════════════\n"
              << "  Dog Wellbeing Module shutdown complete.\n"
              << "═══════════════════════════════════════════════════════\n";
    return 0;
}
