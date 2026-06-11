#include "scheduled_task_manager/jobs/summary_jobs.hpp"
#include <iostream>

namespace oro::stm::jobs {

JobResult daily_pet_summary_generator(const nlohmann::json& /*config*/,
                                      storage_handoff::StorageWriter& /*writer*/) {
    // TODO: Implement daily summaries.
    std::cout << "[STUB] Executing daily_pet_summary_generator...\n";
    JobResult res;
    res.success = true;
    res.items_processed = 0;
    res.metadata["stub"] = true;
    return res;
}

JobResult weekly_pet_summary_generator(const nlohmann::json& /*config*/,
                                       storage_handoff::StorageWriter& /*writer*/) {
    // TODO: Implement weekly summaries.
    std::cout << "[STUB] Executing weekly_pet_summary_generator...\n";
    JobResult res;
    res.success = true;
    res.items_processed = 0;
    res.metadata["stub"] = true;
    return res;
}

void prepare_summary_job_statements(storage_handoff::StorageWriter& /*writer*/) {
    // No prepared statements for stub
}

} // namespace oro::stm::jobs
