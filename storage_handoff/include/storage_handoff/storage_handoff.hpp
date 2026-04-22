#ifndef STORAGE_HANDOFF_STORAGE_HANDOFF_HPP
#define STORAGE_HANDOFF_STORAGE_HANDOFF_HPP

#include "storage_handoff/signal_record.hpp"

namespace storage_handoff {

/**
 * @brief Interacts directly with the target database.
 * For now, this is a mocked library layer containing simple print statements
 * simulating a DB insertion of the SignalRecord.
 */
class StorageWriter {
public:
  StorageWriter() = default;
  ~StorageWriter() = default;

  /**
   * @brief Insterts a valid SignalRecord row into the DB 'signals' table.
   * @param record The normalized record structure.
   * @return true if write succeeded, false otherwise.
   */
  bool insert_signal(const SignalRecord &record);
  std::string unix_ms_to_iso8601(uint64_t unix_ms);
};

} // namespace storage_handoff
#endif // STORAGE_HANDOFF_STORAGE_HANDOFF_HPP
