#include "dog_wellbeing_monitor.hpp"
#include <chrono>
#include <cmath>
#include <iostream>
#include <random>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <nlohmann/json.hpp>

DogWellbeingMonitor::DogWellbeingMonitor(storage_handoff::StorageWriter& writer, 
                                         const std::string& device_id, 
                                         const std::string& dog_id,
                                         uint64_t dummy_interval_ms,
                                         const std::vector<MealSchedule>& meal_schedules,
                                         const std::string& aggregation_window)
    : writer_(writer), device_id_(device_id), dog_id_(dog_id), dummy_interval_ms_(dummy_interval_ms), 
      meal_schedules_(meal_schedules), aggregation_window_(aggregation_window) {}

uint64_t DogWellbeingMonitor::now_ms() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
}

void DogWellbeingMonitor::emit_signal(const SignalRecord& record) {
    std::string obs_at = storage_handoff::StorageWriter::unix_ms_to_iso8601(record.observed_at);
    std::string ing_at = storage_handoff::StorageWriter::unix_ms_to_iso8601(record.ingested_at);
    
    std::optional<std::string> boolean_opt;
    if (record.signal_value_boolean.has_value()) {
        boolean_opt = *record.signal_value_boolean ? "true" : "false";
    }
    
    std::optional<std::string> dog_id_opt;
    if (!record.dog_id.empty()) {
        dog_id_opt = record.dog_id;
    }

    writer_.execute_prepared("insert_signal", record.signal_id, record.device_id, dog_id_opt,
                             record.signal_type, record.signal_value_numeric,
                             record.signal_value_text, boolean_opt, record.unit,
                             obs_at, ing_at, record.source, record.confidence,
                             record.metadata);
}

void DogWellbeingMonitor::update_bowl_weight(const std::string& bowl_id, double weight_grams, uint64_t current_time_ms) {
    auto& tracker = bowl_trackers_[bowl_id];
    
    if (tracker.current_weight == 0.0) {
        tracker.current_weight = weight_grams;
        tracker.last_stable_weight = weight_grams;
        return;
    }

    // Check if the weight has increased significantly (food added/served)
    if (weight_grams > tracker.current_weight + 10.0) {
        double added_quantity = weight_grams - tracker.current_weight;
        
        // 1. Log served_food_quantity (#117)
        std::string event_time = storage_handoff::StorageWriter::unix_ms_to_iso8601(current_time_ms);
        nlohmann::json meta_quantity = {
            {"bowl_id", bowl_id},
            {"event_time", event_time}
        };

        SignalRecord rec_quantity;
        rec_quantity.signal_id = 117;
        rec_quantity.device_id = device_id_;
        rec_quantity.dog_id = dog_id_;
        rec_quantity.signal_type = "served_food_quantity";
        rec_quantity.signal_value_numeric = added_quantity;
        rec_quantity.unit = "grams";
        rec_quantity.observed_at = current_time_ms;
        rec_quantity.ingested_at = now_ms();
        rec_quantity.source = "DW";
        rec_quantity.metadata = meta_quantity.dump();

        emit_signal(rec_quantity);
        std::cout << "[DW] Food addition detected! Logged served_food_quantity: " 
                  << added_quantity << " grams on bowl: " << bowl_id << "\n";

        // 2. Log food_served_confirmation (#119)
        std::string confirmed_at = storage_handoff::StorageWriter::unix_ms_to_iso8601(current_time_ms);
        nlohmann::json meta_confirmation = {
            {"bowl_id", bowl_id},
            {"confirmed_at", confirmed_at},
            {"verification_source", "load_cell"}
        };

        SignalRecord rec_confirm;
        rec_confirm.signal_id = 119;
        rec_confirm.device_id = device_id_;
        rec_confirm.dog_id = dog_id_;
        rec_confirm.signal_type = "food_served_confirmation";
        rec_confirm.signal_value_boolean = true;
        rec_confirm.unit = "boolean";
        rec_confirm.observed_at = current_time_ms;
        rec_confirm.ingested_at = now_ms();
        rec_confirm.source = "DW";
        rec_confirm.metadata = meta_confirmation.dump();

        emit_signal(rec_confirm);
        std::cout << "[DW] Logged food_served_confirmation: TRUE on bowl: " << bowl_id << "\n";
    }

    tracker.current_weight = weight_grams;
    tracker.last_stable_weight = weight_grams;
}

uint64_t DogWellbeingMonitor::get_occurrence_ms(uint64_t current_time_ms, const std::string& time_str, bool next_day) {
    std::time_t t = static_cast<std::time_t>(current_time_ms / 1000);
    if (next_day) {
        t += 24 * 3600;
    }
    std::tm tm_val;
    localtime_r(&t, &tm_val);

    int h = 0, m = 0, s = 0;
    std::sscanf(time_str.c_str(), "%d:%d:%d", &h, &m, &s);

    tm_val.tm_hour = h;
    tm_val.tm_min = m;
    tm_val.tm_sec = s;
    tm_val.tm_isdst = -1;

    std::time_t occ_t = std::mktime(&tm_val);
    return static_cast<uint64_t>(occ_t) * 1000;
}

bool DogWellbeingMonitor::find_next_meal(uint64_t current_time_ms, MealSchedule& next_sched, uint64_t& next_start_ms, uint64_t& next_end_ms) {
    if (meal_schedules_.empty()) {
        return false;
    }

    uint64_t best_start_ms = 0;
    MealSchedule best_sched;
    bool found = false;

    // 1. Check for meals today in the future
    for (const auto& sched : meal_schedules_) {
        uint64_t start_ms = get_occurrence_ms(current_time_ms, sched.start_time, false);
        if (start_ms > current_time_ms) {
            if (!found || start_ms < best_start_ms) {
                best_start_ms = start_ms;
                best_sched = sched;
                found = true;
            }
        }
    }

    // 2. If none today, check tomorrow
    if (!found) {
        for (const auto& sched : meal_schedules_) {
            uint64_t start_ms = get_occurrence_ms(current_time_ms, sched.start_time, true);
            if (!found || start_ms < best_start_ms) {
                best_start_ms = start_ms;
                best_sched = sched;
                found = true;
            }
        }
    }

    if (found) {
        next_sched = best_sched;
        next_start_ms = best_start_ms;

        bool end_next_day = false;
        int sh, sm, ss, eh, em, es;
        std::sscanf(best_sched.start_time.c_str(), "%d:%d:%d", &sh, &sm, &ss);
        std::sscanf(best_sched.end_time.c_str(), "%d:%d:%d", &eh, &em, &es);
        if (eh * 3600 + em * 60 + es < sh * 3600 + sm * 60 + ss) {
            end_next_day = true;
        }

        std::time_t start_t = static_cast<std::time_t>(next_start_ms / 1000);
        if (end_next_day) {
            start_t += 24 * 3600;
        }
        std::tm start_tm;
        localtime_r(&start_t, &start_tm);
        start_tm.tm_hour = eh;
        start_tm.tm_min = em;
        start_tm.tm_sec = es;
        start_tm.tm_isdst = -1;
        next_end_ms = static_cast<uint64_t>(std::mktime(&start_tm)) * 1000;

        return true;
    }

    return false;
}

void DogWellbeingMonitor::initialize_next_meal(uint64_t current_time_ms) {
    MealSchedule next_sched;
    uint64_t next_start_ms = 0;
    uint64_t next_end_ms = 0;
    
    if (find_next_meal(current_time_ms, next_sched, next_start_ms, next_end_ms)) {
        active_sched_ = next_sched;
        active_meal_start_ms_ = next_start_ms;
        active_meal_end_ms_ = next_end_ms;
        monitor_state_ = MonitorState::WAITING;

        std::time_t start_t = static_cast<std::time_t>(active_meal_start_ms_ / 1000);
        std::tm start_tm;
        localtime_r(&start_t, &start_tm);
        char time_buf[64];
        std::strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", &start_tm);

        std::cout << "[DW] Next scheduled meal set: '" << active_sched_.name 
                  << "' starting at " << time_buf << " (bowl: " << active_sched_.bowl_id << ")\n";
    } else {
        std::cerr << "[DW] Error: No meal schedules loaded!\n";
    }
}

void DogWellbeingMonitor::tick(uint64_t current_time_ms) { 
    // 2. Scheduled Meal State Machine
    if (active_meal_start_ms_ == 0) {
        initialize_next_meal(current_time_ms);
        return;
    }

    if (monitor_state_ == MonitorState::WAITING) {
        if (current_time_ms >= active_meal_start_ms_) {
            monitor_state_ = MonitorState::RECORDING;
            
            double current_bowl_weight = bowl_trackers_[active_sched_.bowl_id].current_weight;
            initial_weight_at_start_ = current_bowl_weight;

            std::time_t start_t = static_cast<std::time_t>(active_meal_start_ms_ / 1000);
            std::tm start_tm;
            localtime_r(&start_t, &start_tm);
            char date_buf[32];
            std::strftime(date_buf, sizeof(date_buf), "%Y%m%d", &start_tm);
            std::string date_suffix(date_buf);
            
            std::string full_meal_id = active_sched_.meal_id + date_suffix;
            active_csv_filename_ = "meal_" + full_meal_id + ".csv";

            std::cout << "[DW] Meal '" << active_sched_.name << "' has started! Logging to " << active_csv_filename_ << "\n";
            std::cout << "     Initial weight of " << active_sched_.bowl_id << " is " << initial_weight_at_start_ << "g\n";

            std::ofstream csv_file(active_csv_filename_, std::ios::out | std::ios::trunc);
            if (csv_file.is_open()) {
                char time_buf[64];
                std::strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", &start_tm);
                csv_file << time_buf << ", " << initial_weight_at_start_ << "\n";
            }

            last_csv_write_time_ms_ = current_time_ms;
        }
    } else if (monitor_state_ == MonitorState::RECORDING) {
        if (current_time_ms >= active_meal_end_ms_) {
            double final_weight = bowl_trackers_[active_sched_.bowl_id].current_weight;
            double intake = initial_weight_at_start_ - final_weight;
            
            std::cout << "[DW] Meal '" << active_sched_.name << "' has ended! Final weight: " << final_weight << "g\n";
            std::cout << "     Total food intake: " << intake << "g\n";

            std::ofstream csv_file(active_csv_filename_, std::ios::out | std::ios::app);
            if (csv_file.is_open()) {
                std::time_t end_t = static_cast<std::time_t>(active_meal_end_ms_ / 1000);
                std::tm end_tm;
                localtime_r(&end_t, &end_tm);
                char time_buf[64];
                std::strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", &end_tm);
                csv_file << time_buf << ", " << final_weight << "\n";
            }

            std::time_t start_t = static_cast<std::time_t>(active_meal_start_ms_ / 1000);
            std::tm start_tm;
            localtime_r(&start_t, &start_tm);
            char date_buf[32];
            std::strftime(date_buf, sizeof(date_buf), "%Y%m%d", &start_tm);
            std::string date_suffix(date_buf);
            std::string full_meal_id = active_sched_.meal_id + date_suffix;

            int tolerance_window_min = get_tolerance_window_minutes(active_sched_.start_time, active_sched_.end_time);

            if (intake > 5.0) {
                // 1. Emit food_intake_per_meal
                SignalRecord rec;
                rec.signal_id = 1;
                rec.device_id = device_id_;
                rec.dog_id = dog_id_;
                rec.signal_type = "food_intake_per_meal";
                rec.signal_value_numeric = intake;
                rec.unit = "grams";
                rec.observed_at = active_meal_start_ms_;
                rec.ingested_at = now_ms();
                rec.source = "system";
                
                nlohmann::json meta = {
                    {"signal_id", 1},
                    {"meal_id", full_meal_id},
                    {"bowl_id", active_sched_.bowl_id},
                    {"session_start_time", active_meal_start_ms_},
                    {"session_end_time", active_meal_end_ms_}
                };
                rec.metadata = meta.dump();
                emit_signal(rec);
                std::cout << "[DW] Signal 'food_intake_per_meal' successfully saved to database.\n";

                // 2. Derive and Emit food_intake_per_day
                // Calculate start of today (midnight 00:00:00 local time)
                std::time_t now_t = static_cast<std::time_t>(current_time_ms / 1000);
                std::tm now_tm;
                localtime_r(&now_t, &now_tm);
                now_tm.tm_hour = 0;
                now_tm.tm_min = 0;
                now_tm.tm_sec = 0;
                now_tm.tm_isdst = -1;
                uint64_t start_of_day_ms = static_cast<uint64_t>(std::mktime(&now_tm)) * 1000;
                std::string start_of_day_iso = storage_handoff::StorageWriter::unix_ms_to_iso8601(start_of_day_ms);

                // Fetch total daily food intake before this meal (excluding the current meal ID)
                double previous_meals_sum = writer_.query_double("get_daily_food_intake", dog_id_, start_of_day_iso, full_meal_id);
                double daily_total = previous_meals_sum + intake;

                char date_str_buf[32];
                std::strftime(date_str_buf, sizeof(date_str_buf), "%Y-%m-%d", &now_tm);
                std::string date_str(date_str_buf);

                char tz_buf[32];
                std::strftime(tz_buf, sizeof(tz_buf), "%z", &now_tm);
                std::string tz_str(tz_buf);
                // format standard offset like "+05:30" if %z returns "+0530"
                if (tz_str.length() == 5 && (tz_str[0] == '+' || tz_str[0] == '-')) {
                    tz_str = tz_str.substr(0, 3) + ":" + tz_str.substr(3);
                }

                SignalRecord rec_day;
                rec_day.signal_id = 2;
                rec_day.device_id = device_id_;
                rec_day.dog_id = dog_id_;
                rec_day.signal_type = "food_intake_per_day";
                rec_day.signal_value_numeric = daily_total;
                rec_day.unit = "grams";
                rec_day.observed_at = current_time_ms;
                rec_day.ingested_at = now_ms();
                rec_day.source = "system";
                rec_day.metadata = R"({"date":")" + date_str + 
                                    R"(","timezone":")" + tz_str + 
                                    R"(","aggregation_window":")" + aggregation_window_ + R"("})";
                emit_signal(rec_day);
                std::cout << "[DW] Signal 'food_intake_per_day' successfully saved. Total: " << daily_total << "g\n";

                // // 3. Emit meal_skipped = false
                // SignalRecord rec_skip;
                // rec_skip.device_id = device_id_;
                // rec_skip.dog_id = dog_id_;
                // rec_skip.signal_type = "meal_skipped";
                // rec_skip.signal_value_boolean = false;
                // rec_skip.unit = "boolean";
                // rec_skip.observed_at = active_meal_start_ms_;
                // rec_skip.ingested_at = now_ms();
                // rec_skip.source = "system";
                
                // nlohmann::json meta_skip = {
                //     {"meal_id", full_meal_id},
                //     {"scheduled_meal_time", active_sched_.start_time},
                //     {"tolerance_window_minutes", tolerance_window_min}
                // };
                // rec_skip.metadata = meta_skip.dump();
                // emit_signal(rec_skip);
                // std::cout << "[DW] Signal 'meal_skipped' (value: false) successfully saved to database.\n";
            } else {
                // Dog skipped the meal! Emit meal_skipped = true
                SignalRecord rec_skip;
                rec_skip.signal_id = 3;
                rec_skip.device_id = device_id_;
                rec_skip.dog_id = dog_id_;
                rec_skip.signal_type = "meal_skipped";
                rec_skip.signal_value_boolean = true;
                rec_skip.unit = "boolean";
                rec_skip.observed_at = active_meal_start_ms_;
                rec_skip.ingested_at = now_ms();
                rec_skip.source = "DW";
                
                nlohmann::json meta_skip = {
                    {"meal_id", full_meal_id},
                    {"scheduled_meal_time", active_sched_.start_time},
                    {"tolerance_window_minutes", tolerance_window_min}
                };
                rec_skip.metadata = meta_skip.dump();
                emit_signal(rec_skip);
                std::cout << "[DW] Signal 'meal_skipped' (value: true) successfully saved to database.\n";
            }

            initialize_next_meal(current_time_ms);
        } else {
            if (current_time_ms - last_csv_write_time_ms_ >= 1000) {
                double current_bowl_weight = bowl_trackers_[active_sched_.bowl_id].current_weight;
                std::ofstream csv_file(active_csv_filename_, std::ios::out | std::ios::app);
                if (csv_file.is_open()) {
                    std::time_t t = static_cast<std::time_t>(current_time_ms / 1000);
                    std::tm tm_val;
                    localtime_r(&t, &tm_val);
                    char time_buf[64];
                    std::strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", &tm_val);
                    csv_file << time_buf << ", " << current_bowl_weight << "\n";
                }
                last_csv_write_time_ms_ = current_time_ms;
            }
        }
    }
}

int DogWellbeingMonitor::get_tolerance_window_minutes(const std::string& start_str, const std::string& end_str) {
    int sh = 0, sm = 0, ss = 0;
    int eh = 0, em = 0, es = 0;
    std::sscanf(start_str.c_str(), "%d:%d:%d", &sh, &sm, &ss);
    std::sscanf(end_str.c_str(), "%d:%d:%d", &eh, &em, &es);

    int start_sec = sh * 3600 + sm * 60 + ss;
    int end_sec = eh * 3600 + em * 60 + es;
    if (end_sec < start_sec) {
        end_sec += 24 * 3600; // spans midnight
    }
    return (end_sec - start_sec) / 60;
}

void DogWellbeingMonitor::update_water_level(double water_level_ml, uint64_t current_time_ms) {
    std::time_t now_t = static_cast<std::time_t>(current_time_ms / 1000);
    std::tm local_tm;
    localtime_r(&now_t, &local_tm);

    bool within_window = true;
    int current_time_unit = local_tm.tm_yday;

    if (aggregation_window_ == "24h") {
        int seconds_since_midnight = local_tm.tm_hour * 3600 + local_tm.tm_min * 60 + local_tm.tm_sec;
        within_window = (seconds_since_midnight >= 60) && (seconds_since_midnight <= 86399); // 12:01 AM (00:01:00) to 11:59:59 PM (23:59:59)
    } else if (aggregation_window_ == "1m" || aggregation_window_ == "1min") {
        current_time_unit = local_tm.tm_min; // Reset every calendar minute transition
    } else if (aggregation_window_ == "1h") {
        current_time_unit = local_tm.tm_hour; // Reset every calendar hour transition
    }

    // Check if transition has occurred since last reading to reset accumulated total
    if (last_water_day_yday_ == -1) {
        last_water_day_yday_ = current_time_unit;
    } else if (current_time_unit != last_water_day_yday_) {
        // Log final aggregated total to database at the end of the completed aggregation window
        if (daily_water_intake_ > 0.0) {
            // Get time structure for the window that just completed (e.g., 5 seconds ago)
            std::time_t prev_t = static_cast<std::time_t>((current_time_ms - 5000) / 1000);
            std::tm prev_tm;
            localtime_r(&prev_t, &prev_tm);

            char date_str_buf[32];
            std::strftime(date_str_buf, sizeof(date_str_buf), "%Y-%m-%d", &prev_tm);
            std::string date_str(date_str_buf);

            char tz_buf[32];
            std::strftime(tz_buf, sizeof(tz_buf), "%z", &prev_tm);
            std::string tz_str(tz_buf);
            if (tz_str.length() == 5 && (tz_str[0] == '+' || tz_str[0] == '-')) {
                tz_str = tz_str.substr(0, 3) + ":" + tz_str.substr(3);
            }

            SignalRecord rec;
            rec.signal_id = 4;
            rec.device_id = device_id_;
            rec.dog_id = dog_id_;
            rec.signal_type = "water_intake_per_day";
            rec.signal_value_numeric = daily_water_intake_;
            rec.unit = "ml";
            rec.observed_at = current_time_ms;
            rec.ingested_at = now_ms();
            rec.source = "DW";
            
            nlohmann::json meta = {
                {"date", date_str},
                {"timezone", tz_str},
                {"aggregation_window", aggregation_window_}
            };
            rec.metadata = meta.dump();

            emit_signal(rec);
            std::cout << "[DW] [AGGREGATE] Logged final water_intake_per_day for completed window: " 
                      << daily_water_intake_ << " ml\n";
        }

        daily_water_intake_ = 0.0;
        max_water_level_ = -1.0;
        running_refill_accumulated_intake_ = 0.0;
        last_water_level_ = -1.0;
        last_water_day_yday_ = current_time_unit;
    }

    // Initialize baseline peak level on the first reading of the window
    if (max_water_level_ < 0.0) {
        max_water_level_ = water_level_ml;
        last_water_level_ = water_level_ml;
    }

    // Refill detection: sudden, significant increase of raw reading over the current peak
    if (water_level_ml > max_water_level_ + 15.0) {
        double previous_cycle_intake = max_water_level_ - last_water_level_;
        if (previous_cycle_intake > 0.0) {
            running_refill_accumulated_intake_ += previous_cycle_intake;
        }
        max_water_level_ = water_level_ml;
        last_water_level_ = water_level_ml;
    }
    // Upward noise fluctuation/drift: adjust peak to avoid integrating noise
    else if (water_level_ml > max_water_level_) {
        max_water_level_ = water_level_ml;
    }

    // Compute raw, instant cumulative intake
    double current_intake = running_refill_accumulated_intake_ + (max_water_level_ - water_level_ml);
    if (current_intake < 0.0) {
        current_intake = 0.0;
    }

    // Update in-memory state on any true intake increase during the active window
    if (within_window && current_intake > daily_water_intake_) {
        double diff = current_intake - daily_water_intake_;
        daily_water_intake_ = current_intake;
        std::cout << "[DW] Cumulative water intake updated: " << daily_water_intake_ 
                  << " ml (observed drop of " << diff << " ml)\n";
    }

    // Check if water refill is required (< 800 ml)
    bool refill_required = (water_level_ml < 800.0);
    if (first_refill_check_ || (refill_required != last_refill_required_)) {
        first_refill_check_ = false;
        last_refill_required_ = refill_required;

        if (refill_required) {
            double percent = (water_level_ml / 5000.0) * 100.0;
            if (percent < 0.0) percent = 0.0;
            if (percent > 100.0) percent = 100.0;

            char event_time_buf[64];
            std::strftime(event_time_buf, sizeof(event_time_buf), "%Y-%m-%dT%H:%M:%S", &local_tm);
            std::string event_time_str(event_time_buf);
            
            char tz_offset_buf[16];
            std::strftime(tz_offset_buf, sizeof(tz_offset_buf), "%z", &local_tm);
            std::string tz_offset_str(tz_offset_buf);
            if (tz_offset_str.length() == 5 && (tz_offset_str[0] == '+' || tz_offset_str[0] == '-')) {
                tz_offset_str = tz_offset_str.substr(0, 3) + ":" + tz_offset_str.substr(3);
            }
            event_time_str += tz_offset_str;

            SignalRecord rec_refill;
            rec_refill.signal_id = 129;
            rec_refill.device_id = device_id_;
            rec_refill.dog_id = dog_id_;
            rec_refill.signal_type = "water_refill_required";
            rec_refill.signal_value_boolean = refill_required;
            rec_refill.unit = "boolean";
            rec_refill.observed_at = current_time_ms;
            rec_refill.ingested_at = now_ms();
            rec_refill.source = "DW";

            nlohmann::json meta_refill = {
                {"reservoir_id", "tank"},
                {"event_time", event_time_str},
                {"current_level_percent", percent}
            };
            rec_refill.metadata = meta_refill.dump();

            emit_signal(rec_refill);
            std::cout << "[DW] Logged state transition for water_refill_required: TRUE (Level: " << water_level_ml 
                      << " ml, Percent: " << percent << "%)\n";
        } else {
            std::cout << "[DW] State transition for water_refill_required: FALSE (Level: " << water_level_ml 
                      << " ml) - Database logging skipped as per policy.\n";
        }
    }

    last_water_level_ = water_level_ml;
}
