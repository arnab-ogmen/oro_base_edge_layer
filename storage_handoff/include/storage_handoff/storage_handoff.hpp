#ifndef STORAGE_HANDOFF_STORAGE_HANDOFF_HPP
#define STORAGE_HANDOFF_STORAGE_HANDOFF_HPP

#include "storage_handoff/signal_record.hpp"
#include <pqxx/pqxx>
#include <memory>
#include <string>

#include <chrono>

namespace storage_handoff {

/**
 * @brief Interacts directly with the target PostgreSQL database.
 */
class StorageWriter {
public:
  StorageWriter();
  ~StorageWriter() = default;

  /**
   * @brief Inserts a valid SignalRecord row into the DB table.
   * @param record The normalized record structure.
   * @return true if write succeeded, false otherwise.
   */
  bool insert_signal(const SignalRecord &record);
  
  std::string unix_ms_to_iso8601(uint64_t unix_ms);

private:
  void connect();
  void prepare_statements();
  void ensure_connection();

  std::string conn_str_;
  std::unique_ptr<pqxx::connection> conn_;
  std::chrono::steady_clock::time_point last_reconnect_attempt_;
};

} // namespace storage_handoff
#endif // STORAGE_HANDOFF_STORAGE_HANDOFF_HPP
