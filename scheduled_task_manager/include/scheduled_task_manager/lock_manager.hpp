#ifndef SCHEDULED_TASK_MANAGER_LOCK_MANAGER_HPP
#define SCHEDULED_TASK_MANAGER_LOCK_MANAGER_HPP

#include "storage_handoff/storage_handoff.hpp"
#include <string>

namespace oro::stm {

/**
 * @brief Manages database-level locks for Scheduled Task Manager.
 *
 * Horizontal Scaling Safety:
 *   Only one instance can run a specific job at a time. The database table
 *   stm_job_locks acts as the distributed lock registry.
 */
class LockManager {
public:
    LockManager(storage_handoff::StorageWriter& writer, std::string owner_id);
    ~LockManager() = default;

    /**
     * @brief Prepare the SQL statements required for locking.
     */
    void prepare_statements();

    /**
     * @brief Attempt to acquire lock for a job with a given TTL.
     * @return true if acquired successfully, false if already locked.
     */
    bool try_acquire(const std::string& job_name, int ttl_seconds);

    /**
     * @brief Release lock held by this owner.
     */
    void release(const std::string& job_name);

    /**
     * @brief Extend the lock expiry for long-running jobs (heartbeat).
     */
    void extend(const std::string& job_name, int additional_seconds);

private:
    storage_handoff::StorageWriter& writer_;
    std::string owner_id_;
};

} // namespace oro::stm

#endif // SCHEDULED_TASK_MANAGER_LOCK_MANAGER_HPP
