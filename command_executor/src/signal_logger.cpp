#include "command_executor/signal_logger.hpp"
#include <iostream>

namespace oro {

void SignalLogger::log(const Command& cmd) {
    /*
    -- Schema for reference:
    CREATE TABLE public.signals (
        id              BIGSERIAL PRIMARY KEY,
        device_id       UUID NOT NULL,
        dog_id          UUID,
        signal_type     TEXT NOT NULL,
        signal_value_numeric  NUMERIC,
        signal_value_text     TEXT,
        signal_value_boolean  BOOLEAN,
        unit            TEXT,
        observed_at     TIMESTAMPTZ NOT NULL,
        ingested_at     TIMESTAMPTZ,
        source          TEXT,
        confidence      NUMERIC(5,2),
        metadata        JSONB,
        created_at      TIMESTAMPTZ DEFAULT CURRENT_TIMESTAMP
    );

    -- TODO: Uncomment and use the DB connection wrapper/utility once available.
    const char* insert_sql = "INSERT INTO signals (device_id, signal_type, signal_value_numeric, unit, observed_at, ingested_at, source, metadata) "
                             "VALUES ($1, $2, $3, $4, $5, $6, $7, $8);";
    PGresult* res = PQexecParams(conn_, insert_sql, ...);
    */

    std::cout << "[SignalLogger] DB log placeholder called for signal_id=" << cmd.signal_id
              << " (" << cmd.signal_type << ") with status=" << static_cast<int>(cmd.status) << std::endl;
}

} // namespace oro
