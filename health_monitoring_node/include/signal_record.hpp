#ifndef STORAGE_HANDOFF_SIGNAL_RECORD_HPP
#define STORAGE_HANDOFF_SIGNAL_RECORD_HPP

#include <cstdint>
#include <optional>
#include <string>

struct SignalRecord {
  int signal_id;
  std::string device_id;
  std::string dog_id; // Often null/empty for health signals
  std::string signal_type;
  std::optional<double> signal_value_numeric;
  std::optional<bool> signal_value_boolean;
  std::optional<std::string> signal_value_text;
  std::string unit;
  uint64_t observed_at; // Unix epoch milliseconds
  uint64_t ingested_at; // Unix epoch milliseconds
  std::string source = "system";
  std::optional<double> confidence;
  std::string metadata; // JSON representation of related context
};
#endif // STORAGE_HANDOFF_SIGNAL_RECORD_HPP
