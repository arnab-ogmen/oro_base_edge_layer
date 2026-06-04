#ifndef STORAGE_HANDOFF_STORAGE_HANDOFF_HPP
#define STORAGE_HANDOFF_STORAGE_HANDOFF_HPP

#include <chrono>
#include <iostream>
#include <memory>
#include <pqxx/pqxx>
#include <string>
#include <unordered_map>
#include <mutex>

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
    std::lock_guard<std::recursive_mutex> lock(mutex_);
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

  /**
   * @brief Executes a prepared query that returns a single double result.
   */
  template <typename... Args>
  double query_double(const std::string &stmt_name, Args &&...args) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    try {
      ensure_connection();
      if (!conn_ || !conn_->is_open()) {
        return 0.0;
      }

      pqxx::work txn(*conn_);
      pqxx::result res = txn.exec_prepared(stmt_name, std::forward<Args>(args)...);
      txn.commit();

      if (!res.empty() && !res[0][0].is_null()) {
        return res[0][0].as<double>();
      }
      return 0.0;
    } catch (const std::exception &e) {
      std::cerr << "❌ Query failed for '" << stmt_name << "': " << e.what()
                << "\n";
      return 0.0;
    }
  }

  /**
   * @brief Executes a prepared query that returns a single integer result.
   * Useful for COUNT(*) queries.
   */
  template <typename... Args>
  int query_int(const std::string &stmt_name, Args &&...args) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    try {
      ensure_connection();
      if (!conn_ || !conn_->is_open()) {
        return 0;
      }

      pqxx::work txn(*conn_);
      pqxx::result res = txn.exec_prepared(stmt_name, std::forward<Args>(args)...);
      txn.commit();

      if (!res.empty() && !res[0][0].is_null()) {
        return res[0][0].as<int>();
      }
      return 0;
    } catch (const std::exception &e) {
      std::cerr << "❌ Query failed for '" << stmt_name << "': " << e.what()
                << "\n";
      return 0;
    }
  }

  /**
   * @brief Executes a prepared query that returns a single int64 result.
   * Useful for epoch-millisecond timestamp queries.
   */
  template <typename... Args>
  int64_t query_int64(const std::string &stmt_name, Args &&...args) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    try {
      ensure_connection();
      if (!conn_ || !conn_->is_open()) {
        return 0;
      }

      pqxx::work txn(*conn_);
      pqxx::result res = txn.exec_prepared(stmt_name, std::forward<Args>(args)...);
      txn.commit();

      if (!res.empty() && !res[0][0].is_null()) {
        return res[0][0].as<int64_t>();
      }
      return 0;
    } catch (const std::exception &e) {
      std::cerr << "❌ Query failed for '" << stmt_name << "': " << e.what()
                << "\n";
      return 0;
    }
  }

  /**
   * @brief Executes a prepared query that returns a single string result.
   * Useful for queries returning status, timestamps, or single text fields.
   */
  template <typename... Args>
  std::string query_string(const std::string &stmt_name, Args &&...args) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    try {
      ensure_connection();
      if (!conn_ || !conn_->is_open()) {
        return "";
      }

      pqxx::work txn(*conn_);
      pqxx::result res = txn.exec_prepared(stmt_name, std::forward<Args>(args)...);
      txn.commit();

      if (!res.empty() && !res[0][0].is_null()) {
        return res[0][0].as<std::string>();
      }
      return "";
    } catch (const std::exception &e) {
      std::cerr << "❌ Query failed for '" << stmt_name << "': " << e.what()
                << "\n";
      return "";
    }
  }

  /**
   * @brief Executes a prepared statement and returns the number of affected rows.
   * Useful for DELETE/UPDATE with cleanup jobs.
   */
  template <typename... Args>
  int execute_prepared_count(const std::string &stmt_name, Args &&...args) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    try {
      ensure_connection();
      if (!conn_ || !conn_->is_open()) {
        return 0;
      }

      pqxx::work txn(*conn_);
      pqxx::result res = txn.exec_prepared(stmt_name, std::forward<Args>(args)...);
      txn.commit();

      return static_cast<int>(res.affected_rows());
    } catch (const std::exception &e) {
      std::cerr << "❌ Execute count failed for '" << stmt_name << "': " << e.what()
                << "\n";
      return 0;
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
  mutable std::recursive_mutex mutex_;
};

} // namespace storage_handoff
#endif // STORAGE_HANDOFF_STORAGE_HANDOFF_HPP
