#include "storage_handoff/storage_handoff.hpp"
#include <chrono>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <thread>

namespace storage_handoff {

StorageWriter::StorageWriter()
    : conn_str_("host=localhost user=oro_user password=ogmen dbname=oro_base_db"),
      last_reconnect_attempt_(std::chrono::steady_clock::time_point::min()) {
    connect();
}

void StorageWriter::connect() {
    // Attempt connection. Do not block infinitely in the constructor to allow the node to start,
    // but try to establish the connection or we will retry during insert.
    try {
        conn_ = std::make_unique<pqxx::connection>(conn_str_);
        if (conn_->is_open()) {
            std::cout << "✅ Connected to PostgreSQL (" << conn_str_ << ")\n";
            prepare_statements();
        }
    } catch (const std::exception& e) {
        std::cerr << "❌ DB connection failed: " << e.what() << "\n";
    }
}

void StorageWriter::prepare_statements() {
    try {
        conn_->prepare(
            "insert_signal",
            R"(
            INSERT INTO public.oro_base_signals (
                device_id, dog_id, signal_type,
                signal_value_numeric, signal_value_text, signal_value_boolean,
                unit, observed_at, ingested_at, source, confidence, metadata, created_at
            )
            VALUES (
                $1, $2, $3,
                $4, $5, $6,
                $7, $8, $9, $10, $11, $12::jsonb, NOW()
            )
            )"
        );
    } catch(const std::exception& e) {
         std::cerr << "❌ DB prepare failed: " << e.what() << "\n";
    }
}

void StorageWriter::ensure_connection() {
    if (!conn_ || !conn_->is_open()) {
        auto now = std::chrono::steady_clock::now();
        if (now - last_reconnect_attempt_ < std::chrono::seconds(5)) {
            return; // Throttle to 1 attempt every 5 seconds
        }
        last_reconnect_attempt_ = now;

        std::cerr << "⚠️ Reconnecting to DB...\n";
        connect();
    }
}

bool StorageWriter::insert_signal(const SignalRecord &record) {
  try {
      ensure_connection();
      
      if (!conn_ || !conn_->is_open()) {
          return false; // Connection could not be established
      }

      pqxx::work txn(*conn_);

      std::string obs_at = unix_ms_to_iso8601(record.observed_at);
      std::string ing_at = unix_ms_to_iso8601(record.ingested_at);

      std::optional<std::string> dog_id_opt;
      if (!record.dog_id.empty()) dog_id_opt = record.dog_id;

      std::optional<std::string> metadata_opt;
      if (!record.metadata.empty()) metadata_opt = record.metadata;

      std::optional<std::string> boolean_opt;
      if (record.signal_value_boolean.has_value()) {
          boolean_opt = *record.signal_value_boolean ? "true" : "false";
      }

      txn.exec_prepared("insert_signal",
          record.device_id,
          dog_id_opt,
          record.signal_type,
          record.signal_value_numeric,
          record.signal_value_text,
          boolean_opt,
          record.unit,
          obs_at,
          ing_at,
          record.source,
          record.confidence,
          metadata_opt
      );

      txn.commit();
      
      return true;

  } catch (const pqxx::broken_connection& e) {
      std::cerr << "⚠️ Lost DB connection: " << e.what() << "\n";
      return false;
  } catch (const std::exception& e) {
      std::cerr << "❌ Insert failed for " << record.signal_type << ": " << e.what() << "\n";
      return false;
  }
}

std::string StorageWriter::unix_ms_to_iso8601(uint64_t unix_ms) {
  auto duration = std::chrono::milliseconds(unix_ms);
  auto time_point =
      std::chrono::time_point<std::chrono::system_clock>(duration);

  std::time_t time = std::chrono::system_clock::to_time_t(time_point);
  uint64_t ms = unix_ms % 1000;

  std::tm tm_struct;
  gmtime_r(&time, &tm_struct);

  std::ostringstream oss;
  oss << std::put_time(&tm_struct, "%Y-%m-%dT%H:%M:%S");
  oss << "." << std::setfill('0') << std::setw(3) << ms << "Z";

  return oss.str();
}

} // namespace storage_handoff
