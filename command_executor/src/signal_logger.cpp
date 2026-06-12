#include "command_executor/signal_logger.hpp"
#include "storage_handoff/storage_handoff.hpp"
#include "data/oro_protocol.hpp"
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <zmq.hpp>

namespace oro {

namespace {

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
    prepared = true;
  }
  return writer_instance;
}

void insert_signal(int signal_id,
                   const std::string &device_id,
                   const std::optional<std::string> &dog_id,
                   const std::string &signal_type,
                   const std::optional<double> &numeric_val,
                   const std::optional<std::string> &text_val,
                   const std::optional<std::string> &bool_text_val,
                   const std::string &unit, const std::string &ts_iso,
                   const std::string &source,
                   const std::string &metadata_json) {
  std::lock_guard<std::mutex> lock(g_db_mutex);
  const bool ok = writer().execute_prepared(
      "insert_command_executor_signal", signal_id, device_id, dog_id, signal_type,
      numeric_val, text_val, bool_text_val, unit, ts_iso, ts_iso, source,
      std::optional<double>{}, metadata_json);
  if (!ok) {
    std::cerr << "[SignalLogger] Failed to write signal " << signal_type
              << " (id: " << signal_id << ")\n";
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

  // 1. Log the inbound command itself to the signals table.
  if (cmd.signal_id == 84 || cmd.signal_id == 85 || cmd.signal_id == 123 ||
      cmd.signal_id == 64 || cmd.signal_id == 91 || cmd.signal_id == 88 ||
      cmd.signal_id == 133 || cmd.signal_id == 98 || cmd.signal_id == 134 ||
      cmd.signal_id == 135 || cmd.signal_id == 137 || cmd.signal_id == 138 ||
      cmd.signal_id == 139 || cmd.signal_id == 136) {

    if (cmd.signal_id == 98) {
      // OS for settings apply flow (#98)
      bool success = (cmd.status == CommandStatus::COMPLETED);
      insert_signal(98, device_id, dog_id, "settings_apply_success_status",
                    std::nullopt, std::nullopt,
                    std::optional<std::string>(success ? "true" : "false"),
                    "boolean", ts_iso, "system", base_payload);
    } else if (cmd.signal_id == 134) {
      // OS for camera rotation (#134)
      insert_signal(134, device_id, dog_id, cmd.signal_type,
                    optional_numeric(cmd.payload, "angle"), std::nullopt, std::nullopt,
                    "degrees", ts_iso, "system", base_payload);
    } else if (cmd.signal_id == 64) {
      // OS for lid actuation command (#64)
      std::optional<std::string> action;
      if (cmd.payload.contains("action") && cmd.payload["action"].is_string()) {
        action = cmd.payload["action"].get<std::string>();
      }
      insert_signal(64, device_id, dog_id, cmd.signal_type,
                    std::nullopt, action, std::nullopt,
                    "action", ts_iso, "system", base_payload);
    } else if (cmd.signal_id == 136) {
      // Log Signal #136 to signals table
      std::optional<std::string> success_str;
      if (cmd.payload.contains("status")) {
        if (cmd.payload["status"].is_string()) {
          success_str = (cmd.payload["status"].get<std::string>() == "success") ? "true" : "false";
        } else if (cmd.payload["status"].is_boolean()) {
          success_str = cmd.payload["status"].get<bool>() ? "true" : "false";
        }
      } else if (cmd.payload.contains("saved")) {
        success_str = cmd.payload["saved"].get<bool>() ? "true" : "false";
      }
      insert_signal(136, device_id, dog_id, "video_file_save_confirmation",
                    std::nullopt, std::nullopt, success_str,
                    "boolean", ts_iso, "system", base_payload);
    } else if (cmd.signal_id == 139) {
      nlohmann::json meta;
      meta["status"] = cmd.result.contains("status") ? cmd.result["status"].get<std::string>() : "acknowledged";
      meta["file_id"] = cmd.result.contains("file_id") ? cmd.result["file_id"].get<std::string>() : "";
      meta["command_id"] = cmd.command_id;
      meta["event_time"] = cmd.event_time;
      meta["storage_path"] = cmd.result.contains("storage_path") ? cmd.result["storage_path"].get<std::string>() : "";

      insert_signal(139, device_id, dog_id, "video_capture_pan_command_event",
                    std::nullopt, std::nullopt, std::nullopt,
                    "event", ts_iso, "UCES", meta.dump());
    } else {
      // Other inbound signals are EVENT type (84, 85, 123, 91, 88, 133, 135)
      insert_signal(cmd.signal_id, device_id, dog_id, cmd.signal_type,
                    std::nullopt, std::nullopt, std::nullopt,
                    "event", ts_iso, "system", base_payload);
    }
  }

  // 2. Log outbound/result/confirmation signals generated as side-effects.

  // Post-UC capture for lid_actuation_result (#65).
  if (cmd.signal_id == 64) {
    const std::string result_payload = cmd.result.dump();
    std::optional<std::string> result_text;
    if (cmd.result.contains("status") && cmd.result["status"].is_string()) {
      result_text = cmd.result["status"].get<std::string>();
    } else {
      result_text =
          (cmd.status == CommandStatus::COMPLETED) ? "SUCCESS" : "FAILED";
    }
    insert_signal(65, device_id, dog_id, "lid_actuation_result", std::nullopt,
                  result_text, std::nullopt, "status", ts_iso, "system",
                  result_payload);
  }

  // Post-UC event + OS for treat dispense (#126 and #125).
  if (cmd.signal_id == 85) {
    const std::string result_payload = cmd.result.dump();

    // Log quantity immediately (#125) to signals table
    insert_signal(125, device_id, dog_id, "treat_dispensed_quantity",
                  optional_numeric(cmd.result, "treats_dispensed"),
                  std::nullopt, std::nullopt, "count", ts_iso, "system",
                  result_payload);

    // Spawn thread to log confirmation after 5 seconds (#126)
    std::thread([device_id, dog_id, cmd]() {
      std::this_thread::sleep_for(std::chrono::seconds(5));

      const bool success = (cmd.status == CommandStatus::COMPLETED);
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

      // Log to signals table
      insert_signal(126, device_id, dog_id, "treat_dispense_confirmation",
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
    insert_signal(93, device_id, dog_id, "image_file_save_confirmation",
                  std::nullopt, std::nullopt,
                  optional_bool_text(cmd.result, "saved"), "boolean", ts_iso,
                  "system", result_payload);
  }
}

} // namespace oro
