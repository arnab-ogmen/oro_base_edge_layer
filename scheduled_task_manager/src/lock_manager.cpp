#include "scheduled_task_manager/lock_manager.hpp"
#include <chrono>
#include <iostream>

namespace oro::stm {

LockManager::LockManager(storage_handoff::StorageWriter& writer, std::string owner_id)
    : writer_(writer), owner_id_(owner_id) {}

void LockManager::prepare_statements() {
    writer_.prepare("stm_lock_acquire", R"(
        INSERT INTO stm_job_locks (job_name, lock_key, locked_until, owner)
        VALUES ($1, $2, NOW() + ($3 || ' seconds')::interval, $4)
        ON CONFLICT (job_name) DO UPDATE
          SET lock_key = EXCLUDED.lock_key,
              locked_at = NOW(),
              locked_until = EXCLUDED.locked_until,
              owner = EXCLUDED.owner
          WHERE stm_job_locks.locked_until < NOW()
        RETURNING job_name
    )");

    writer_.prepare("stm_lock_release", R"(
        DELETE FROM stm_job_locks
        WHERE job_name = $1 AND owner = $2
    )");

    writer_.prepare("stm_lock_extend", R"(
        UPDATE stm_job_locks
        SET locked_until = NOW() + ($2 || ' seconds')::interval
        WHERE job_name = $1 AND owner = $3
    )");
}

bool LockManager::try_acquire(const std::string& job_name, int ttl_seconds) {
    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::system_clock::now().time_since_epoch())
                      .count();
    std::string lock_key = owner_id_ + "_" + std::to_string(now_ms);
    std::string ttl_str = std::to_string(ttl_seconds);

    // query_bool returns true if the INSERT/UPDATE returned a row (lock acquired)
    bool acquired = writer_.query_bool("stm_lock_acquire", job_name, lock_key, ttl_str, owner_id_);
    if (acquired) {
        std::cout << "[LockManager] Acquired lock for '" << job_name << "' (TTL: " << ttl_seconds << "s)\n";
    }
    return acquired;
}

void LockManager::release(const std::string& job_name) {
    bool ok = writer_.execute_prepared("stm_lock_release", job_name, owner_id_);
    if (ok) {
        std::cout << "[LockManager] Released lock for '" << job_name << "'\n";
    } else {
        std::cerr << "[LockManager] Failed to release lock for '" << job_name << "'\n";
    }
}

void LockManager::extend(const std::string& job_name, int additional_seconds) {
    std::string add_str = std::to_string(additional_seconds);
    bool ok = writer_.execute_prepared("stm_lock_extend", job_name, add_str, owner_id_);
    if (ok) {
        std::cout << "[LockManager] Extended lock for '" << job_name << "' by " << additional_seconds << "s\n";
    } else {
        std::cerr << "[LockManager] Failed to extend lock for '" << job_name << "'\n";
    }
}

} // namespace oro::stm
