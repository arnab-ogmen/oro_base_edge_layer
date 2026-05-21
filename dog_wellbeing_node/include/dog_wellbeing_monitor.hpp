#ifndef DOG_WELLBEING_MONITOR_HPP
#define DOG_WELLBEING_MONITOR_HPP

#include "storage_handoff/storage_handoff.hpp"
#include <string>
#include <optional>
#include <unordered_map>
#include <vector>

struct SignalRecord {
    int signal_id;
    std::string device_id;
    std::string dog_id;
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

struct MealSchedule {
    std::string name;
    double intended_quantity_grams;
    std::string start_time;
    std::string end_time;
    std::string meal_id;
    std::string bowl_id;
};

class DogWellbeingMonitor {
public:
    DogWellbeingMonitor(storage_handoff::StorageWriter& writer, 
                        const std::string& device_id, 
                        const std::string& dog_id,
                        uint64_t dummy_interval_ms,
                        const std::vector<MealSchedule>& meal_schedules);

    // Call periodically to generate dummy signals
    void tick(uint64_t current_time_ms);

    // Call when new weight is received from a bowl
    void update_bowl_weight(const std::string& bowl_id, double weight_grams, uint64_t current_time_ms);

private:
    uint64_t now_ms();
    void emit_signal(const SignalRecord& record);
    
    void initialize_next_meal(uint64_t current_time_ms);
    bool find_next_meal(uint64_t current_time_ms, MealSchedule& next_sched, uint64_t& next_start_ms, uint64_t& next_end_ms);
    uint64_t get_occurrence_ms(uint64_t current_time_ms, const std::string& time_str, bool next_day);
    int get_tolerance_window_minutes(const std::string& start_str, const std::string& end_str);

    storage_handoff::StorageWriter& writer_;
    std::string device_id_;
    std::string dog_id_;
    uint64_t dummy_interval_ms_;
    std::vector<MealSchedule> meal_schedules_;

    // Timing state for dummy signals
    uint64_t last_dummy_emit_time_ms_ = 0;

    // Scheduler states
    enum class MonitorState { WAITING, RECORDING };
    MonitorState monitor_state_ = MonitorState::WAITING;

    MealSchedule active_sched_;
    uint64_t active_meal_start_ms_ = 0;
    uint64_t active_meal_end_ms_ = 0;
    double initial_weight_at_start_ = 0.0;
    uint64_t last_csv_write_time_ms_ = 0;
    std::string active_csv_filename_;

    struct BowlTracker {
        double current_weight = 0.0;
        double last_stable_weight = 0.0;
    };

    std::unordered_map<std::string, BowlTracker> bowl_trackers_;

    // Constants for meal detection
    static constexpr double MEAL_START_DROP_THRESHOLD = 5.0; // grams
    static constexpr double WEIGHT_STABLE_THRESHOLD = 2.0;   // grams
    static constexpr uint64_t IDLE_WINDOW_MS = 30000;        // 30 seconds
};

#endif // DOG_WELLBEING_MONITOR_HPP
