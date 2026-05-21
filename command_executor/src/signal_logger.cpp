#include "command_executor/signal_logger.hpp"
#include "storage_handoff/storage_handoff.hpp"
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace oro {

const std::unordered_set<uint16_t> SignalLogger::EVENT_SIGNAL_IDS = {
    84,  // lid_open_command
    123, // lid_close_command
    64,  // lid_actuation_command
    85,  // treat_dispense_command_event
    91,  // photo_capture_command_event
    88,  // live_session_start_event
    133, // live_session_end_event
};

namespace {

/**
 * @brief Choice of event/dedupe identifiers:
 * In the Events table, the 'event_id' is a system-generated UUID.
 * To ensure the Signal ID (#84, #85, etc.) is the primary logical identifier,
 * it is prefixed to the 'command_id' and 'dedupe_key' (e.g., CMD_88_12345).
 */
constexpr const char *kDefaultConnStr =
    "host=localhost user=oro_user password=ogmen dbname=oro_base_db";
constexpr const char *kDefaultDeviceId = "9e092b69-5973-46e4-a228-fe4933e04364";

std::mutex g_db_mutex;

bool is_millis_epoch(int64_t ts) { return ts > 100000000000LL; }

uint64_t normalized_epoch_ms(int64_t event_time) {
  if (event_time <= 0) {
    auto now = std::chrono::system_clock::now();
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch())
            .count());
  }
  if (is_millis_epoch(event_time)) {
    return static_cast<uint64_t>(event_time);
  }
  return static_cast<uint64_t>(event_time) * 1000ULL;
}

std::string resolve_device_id(const Command &cmd) {
  if (cmd.payload.contains("device_id") &&
      cmd.payload["device_id"].is_string()) {
    return cmd.payload["device_id"].get<std::string>();
  }
  if (const char *env_device_id = std::getenv("ORO_DEVICE_ID")) {
    return std::string(env_device_id);
  }
  return kDefaultDeviceId;
}

std::optional<std::string> resolve_dog_id(const Command &cmd) {
  if (cmd.payload.contains("dog_id") && cmd.payload["dog_id"].is_string()) {
    return cmd.payload["dog_id"].get<std::string>();
  }
  return std::nullopt;
}

std::string severity_from_status(const CommandStatus status) {
  switch (status) {
  case CommandStatus::FAILED:
  case CommandStatus::TIMEOUT:
  case CommandStatus::REJECTED:
    return "medium";
  default:
    return "info";
  }
}

std::string status_from_command_status(const CommandStatus status) {
  switch (status) {
  case CommandStatus::COMPLETED:
    return "resolved";
  case CommandStatus::FAILED:
  case CommandStatus::TIMEOUT:
  case CommandStatus::REJECTED:
    return "open";
  default:
    return "open";
  }
}

std::optional<double> optional_numeric(const nlohmann::json &j,
                                       const char *key) {
  if (!j.contains(key)) {
    return std::nullopt;
  }
  if (j[key].is_number_float() || j[key].is_number_integer()) {
    return j[key].get<double>();
  }
  return std::nullopt;
}

std::optional<std::string> optional_bool_text(const nlohmann::json &j,
                                              const char *key) {
  if (!j.contains(key) || !j[key].is_boolean()) {
    return std::nullopt;
  }
  return j[key].get<bool>() ? "true" : "false";
}

storage_handoff::StorageWriter &writer() {
  static storage_handoff::StorageWriter writer_instance(
      std::getenv("ORO_DB_CONN_STR") != nullptr ? std::getenv("ORO_DB_CONN_STR")
                                                : kDefaultConnStr);
  static bool prepared = false;
  if (!prepared) {
    writer_instance.prepare("insert_command_executor_signal",
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

    writer_instance.prepare("insert_command_executor_event",
                            R"(
        INSERT INTO public.oro_base_events (
            device_id, dog_id, event_type, category, event_source, severity, status,
            trigger_mode, detected_at, event_start_at, event_end_at, confidence,
            title, description, payload, trigger_context, root_signal_refs,
            dedupe_key, notification_eligible, created_at, updated_at
        )
        VALUES (
            $1, $2, $3, $4, $5, $6, $7,
            $8, $9, $10, $11, $12,
            $13, $14, $15::jsonb, $16::jsonb, $17::jsonb,
            $18, $19, NOW(), NOW()
        )
        )");
    prepared = true;
  }
  return writer_instance;
}

void insert_event(const std::string &device_id,
                  const std::optional<std::string> &dog_id,
                  const std::string &event_type, const std::string &title,
                  const std::string &severity, const std::string &status,
                  const std::string &ts_iso, const std::string &payload_json,
                  const std::string &trigger_context_json,
                  const std::string &root_signal_refs_json,
                  const std::string &dedupe_key, bool notification_eligible) {
  std::lock_guard<std::mutex> lock(g_db_mutex);
  const bool ok = writer().execute_prepared(
      "insert_command_executor_event", device_id, dog_id, event_type,
      "Device Health", "user_action", severity, status, "real_time", ts_iso,
      ts_iso, std::optional<std::string>{}, std::optional<double>{}, title,
      std::optional<std::string>{}, payload_json, trigger_context_json,
      root_signal_refs_json, dedupe_key, notification_eligible);
  if (!ok) {
    std::cerr << "[SignalLogger] Failed to write event " << event_type << "\n";
  }
}

void insert_signal(const std::string &device_id,
                   const std::optional<std::string> &dog_id,
                   const std::string &signal_type,
                   const std::optional<double> &numeric_val,
                   const std::optional<std::string> &text_val,
                   const std::optional<std::string> &bool_text_val,
                   const std::string &unit, const std::string &ts_iso,
                   const std::string &source,
                   const std::string &metadata_json) {
  int sig_id = 0;
  if (signal_type == "manual_lid_open_command_event") sig_id = 84;
  else if (signal_type == "manual_lid_close_c") sig_id = 123;
  else if (signal_type == "settings_apply_success_status") sig_id = 98;
  else if (signal_type == "lid_actuation_result") sig_id = 65;
  else if (signal_type == "treat_dispensed_quantity") sig_id = 125;
  else if (signal_type == "treat_dispense_confirmation") sig_id = 126;
  else if (signal_type == "image_file_save_confirmation") sig_id = 93;

  std::lock_guard<std::mutex> lock(g_db_mutex);
  const bool ok = writer().execute_prepared(
      "insert_command_executor_signal", sig_id, device_id, dog_id, signal_type,
      numeric_val, text_val, bool_text_val, unit, ts_iso, ts_iso, source,
      std::optional<double>{}, metadata_json);
  if (!ok) {
    std::cerr << "[SignalLogger] Failed to write signal " << signal_type
              << "\n";
  }
}

} // namespace

void SignalLogger::log(const Command &cmd) {
  const auto event_ts_ms = normalized_epoch_ms(cmd.event_time);
  const std::string ts_iso =
      storage_handoff::StorageWriter::unix_ms_to_iso8601(event_ts_ms);
  const std::string device_id = resolve_device_id(cmd);
  const auto dog_id = resolve_dog_id(cmd);
  const std::string base_payload = cmd.payload.dump();
  nlohmann::json trigger_json = {
      {"event_id", cmd.signal_id},     {"event_name", cmd.signal_type},
      {"command_id", cmd.command_id},  {"issued_by", cmd.issued_by},
      {"initiated_by", cmd.issued_by}, {"status", static_cast<int>(cmd.status)},
      {"event_time", cmd.event_time}};
  if (cmd.result.contains("session_id")) {
    trigger_json["session_id"] = cmd.result["session_id"];
  }
  const std::string trigger_context = trigger_json.dump();
  const std::string root_signal_refs =
      nlohmann::json(
          {{"signal_id", cmd.signal_id}, {"signal_type", cmd.signal_type}})
          .dump();

  // OS signals originating from hardware/state observation.
  if (cmd.signal_id == 84 || cmd.signal_id == 123 || cmd.signal_id == 98) {
    if (cmd.signal_id == 98) {
      // OS for settings apply flow (#98)
      bool success = (cmd.status == CommandStatus::COMPLETED);
      insert_signal(device_id, dog_id, "settings_apply_success_status",
                    std::nullopt, std::nullopt,
                    std::optional<std::string>(success ? "true" : "false"),
                    "boolean", ts_iso, "system", base_payload);
    } else {
      insert_signal(device_id, dog_id, cmd.signal_type, std::nullopt,
                    std::nullopt, std::nullopt, "event", ts_iso, "system",
                    base_payload);
    }
  }

  if (SignalLogger::EVENT_SIGNAL_IDS.count(cmd.signal_id)) {
    insert_event(device_id, dog_id, cmd.signal_type, cmd.signal_type,
                 severity_from_status(cmd.status),
                 status_from_command_status(cmd.status), ts_iso, base_payload,
                 trigger_context, root_signal_refs,
                 "CMD_" + std::to_string(cmd.signal_id) + "_" + cmd.command_id,
                 true);
  }

  // Post-UC and OS capture for lid_actuation_result (#65).
  if (cmd.signal_id == 64) {
    const std::string result_payload = cmd.result.dump();
    std::optional<std::string> result_text;
    if (cmd.result.contains("status") && cmd.result["status"].is_string()) {
      result_text = cmd.result["status"].get<std::string>();
    } else {
      result_text =
          (cmd.status == CommandStatus::COMPLETED) ? "SUCCESS" : "FAILED";
    }
    insert_signal(device_id, dog_id, "lid_actuation_result", std::nullopt,
                  result_text, std::nullopt, "status", ts_iso, "system",
                  result_payload);
    insert_event(device_id, dog_id, "lid_actuation_result",
                 "lid_actuation_result", severity_from_status(cmd.status),
                 status_from_command_status(cmd.status), ts_iso, result_payload,
                 trigger_context, root_signal_refs, 
                 "CMD_64_" + cmd.command_id + "_result", true);
  }

  // Post-UC event + OS for treat dispense (#126 and #125).
  if (cmd.signal_id == 85) {
    const std::string result_payload = cmd.result.dump();

    // 1. Log quantity immediately (#125) to both signals and events tables
    insert_signal(device_id, dog_id, "treat_dispensed_quantity",
                  optional_numeric(cmd.result, "treats_dispensed"),
                  std::nullopt, std::nullopt, "count", ts_iso, "system",
                  result_payload);

    insert_event(device_id, dog_id, "treat_dispensed_quantity",
                 "Treat Dispensed Quantity", severity_from_status(cmd.status),
                 status_from_command_status(cmd.status), ts_iso, result_payload,
                 trigger_context, root_signal_refs,
                 "CMD_85_" + cmd.command_id + "_quantity", true);

    // 2. Spawn thread to log confirmation after 5 seconds (#126)
    std::thread([device_id, dog_id, cmd, trigger_context, root_signal_refs]() {
      std::this_thread::sleep_for(std::chrono::seconds(5));

      const bool success = (cmd.status == CommandStatus::COMPLETED);

      // Requirement #126: command_id, confirmed_at, verification_source
      auto now = std::chrono::system_clock::now();
      auto confirmed_at = std::chrono::duration_cast<std::chrono::milliseconds>(
                              now.time_since_epoch())
                              .count();

      nlohmann::json confirm_payload = {
          {"command_id", cmd.command_id},
          {"confirmed_at", confirmed_at},
          {"verification_source", "ir_sensor_feedback"},
          {"status", success ? "success" : "failed"}};
      const std::string confirmation_payload = confirm_payload.dump();
      std::string now_iso =
          storage_handoff::StorageWriter::unix_ms_to_iso8601(confirmed_at);

      // Log to events table
      insert_event(
          device_id, dog_id, "treat_dispense_confirmation",
          "treat_dispense_confirmation", severity_from_status(cmd.status),
          status_from_command_status(cmd.status), now_iso, confirmation_payload,
          trigger_context, root_signal_refs, 
          "CMD_85_" + cmd.command_id + "_confirm", true);

      // Log to signals table too
      insert_signal(device_id, dog_id, "treat_dispense_confirmation",
                    std::nullopt, std::nullopt,
                    std::optional<std::string>(success ? "true" : "false"),
                    "boolean", now_iso, "system", confirmation_payload);

      std::cout
          << "[SignalLogger] Delayed treat dispense confirmation logged for "
          << cmd.command_id << "\n";
    }).detach();
  }

  // OS for photo flow (#93).
  if (cmd.signal_id == 91) {
    const std::string result_payload = cmd.result.dump();
    insert_signal(device_id, dog_id, "image_file_save_confirmation",
                  std::nullopt, std::nullopt,
                  optional_bool_text(cmd.result, "saved"), "boolean", ts_iso,
                  "system", result_payload);
  }
}
} // namespace oro
