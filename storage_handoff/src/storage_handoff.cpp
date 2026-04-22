#include "storage_handoff/storage_handoff.hpp"
#include <chrono>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace storage_handoff {

bool StorageWriter::insert_signal(const SignalRecord &record) {
  std::cout << "[DATABASE HANDOFF] Inserting Record into 'signals' table:\n"
            << "  device_id: " << record.device_id << "\n"
            << "  signal_type: " << record.signal_type << "\n";

  if (record.signal_value_numeric) {
    std::cout << "  value (numeric): " << *record.signal_value_numeric << "\n";
  }
  if (record.signal_value_boolean) {
    std::cout << "  value (boolean): "
              << (*record.signal_value_boolean ? "true" : "false") << "\n";
  }
  if (record.signal_value_text) {
    std::cout << "  value (text): " << *record.signal_value_text << "\n";
  }

  std::cout << "  unit: " << (record.unit.empty() ? "none" : record.unit)
            << "\n"
            << "  observed_at: " << unix_ms_to_iso8601(record.observed_at)
            << " ms\n"
            << "  ingested_at: " << unix_ms_to_iso8601(record.ingested_at)
            << " ms\n"
            << "  source: " << record.source << "\n"
            << "  metadata: " << record.metadata << "\n"
            << "--------------------------------------------------------\n";

  return true; // Simulate success
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
