#include "storage_handoff/storage_handoff.hpp"
#include <chrono>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <thread>

namespace storage_handoff {

StorageWriter::StorageWriter(const std::string& conn_str)
    : conn_str_(conn_str),
      last_reconnect_attempt_(std::chrono::steady_clock::time_point::min()) {
    connect();
}

void StorageWriter::connect() {
    // Attempt connection. Do not block infinitely in the constructor to allow the node to start,
    // but try to establish the connection or we will retry during insert.
    try {
        conn_ = std::make_unique<pqxx::connection>(conn_str_);
        if (conn_->is_open()) {
            std::cout << "✅ Connected to PostgreSQL\n";
            prepare_statements();
        }
    } catch (const std::exception& e) {
        std::cerr << "❌ DB connection failed: " << e.what() << "\n";
    }
}

void StorageWriter::prepare(const std::string& name, const std::string& query) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    prepared_queries_[name] = query;
    if (conn_ && conn_->is_open()) {
        try {
            conn_->prepare(name, query);
        } catch(const std::exception& e) {
            std::cerr << "❌ DB prepare failed for '" << name << "': " << e.what() << "\n";
        }
    }
}

void StorageWriter::prepare_statements() {
    // Register all previously saved queries (useful on reconnect)
    for (const auto& [name, query] : prepared_queries_) {
        try {
            conn_->prepare(name, query);
        } catch(const std::exception& e) {
             std::cerr << "❌ DB prepare failed for '" << name << "': " << e.what() << "\n";
        }
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
