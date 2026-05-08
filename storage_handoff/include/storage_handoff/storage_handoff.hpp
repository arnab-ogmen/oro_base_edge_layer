#ifndef STORAGE_HANDOFF_STORAGE_HANDOFF_HPP
#define STORAGE_HANDOFF_STORAGE_HANDOFF_HPP

#include <chrono>
#include <iostream>
#include <memory>
#include <pqxx/pqxx>
#include <string>
#include <unordered_map>

// TODO: Implement thread-safe operations for concurrent writes from multiple
// threads.
// TODO: Make the database connection credentials configurable.
// TODO: Add retry logic for failed write operations.

namespace storage_handoff {

/**
 * @brief Interacts directly with the target PostgreSQL database.
 */
class StorageWriter {
public:
  explicit StorageWriter(const std::string &conn_str);
  ~StorageWriter() = default;

  /**
   * @brief Registers a prepared statement to be executed later.
   * @param name The name of the prepared statement.
   * @param query The SQL query with placeholders ($1, $2, etc.).
   */
  void prepare(const std::string &name, const std::string &query);

  /**
   * @brief Executes a previously registered prepared statement with arguments.
   * @tparam Args Types of the arguments to pass to the statement.
   * @param stmt_name The name of the prepared statement.
   * @param args The arguments.
   * @return true if execution succeeded, false otherwise.
   */
  template <typename... Args>
  bool execute_prepared(const std::string &stmt_name, Args &&...args) {
    try {
      ensure_connection();
      if (!conn_ || !conn_->is_open()) {
        return false;
      }

      pqxx::work txn(*conn_);
      txn.exec_prepared(stmt_name, std::forward<Args>(args)...);
      txn.commit();

      return true;
    } catch (const pqxx::broken_connection &e) {
      std::cerr << "⚠️ Lost DB connection: " << e.what() << "\n";
      return false;
    } catch (const std::exception &e) {
      std::cerr << "❌ Execute failed for '" << stmt_name << "': " << e.what()
                << "\n";
      return false;
    }
  }

  static std::string unix_ms_to_iso8601(uint64_t unix_ms);

private:
  void connect();
  void prepare_statements();
  void ensure_connection();

  std::string conn_str_;
  std::unique_ptr<pqxx::connection> conn_;
  std::chrono::steady_clock::time_point last_reconnect_attempt_;
  std::unordered_map<std::string, std::string> prepared_queries_;
};

} // namespace storage_handoff
#endif // STORAGE_HANDOFF_STORAGE_HANDOFF_HPP
