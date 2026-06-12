-- Local ORO Base Schema (PostgreSQL)
--
-- This local database mirrors the cloud ORM model for edge use, including
-- locally stored user and user-device-access rows.
-- Ownership columns reference local tables where they exist.

CREATE EXTENSION IF NOT EXISTS "pgcrypto";

-- =========================================================
-- 0. USERS AND ACCESS (Cloud -> Edge)
-- =========================================================

CREATE TABLE IF NOT EXISTS oro_base_user (
    user_id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    device_id UUID,
    full_name TEXT,
    email TEXT NOT NULL UNIQUE,
    phone_number TEXT,
    auth_provider TEXT,
    auth_provider_user_id TEXT,
    password_hash TEXT,
    is_email_verified BOOLEAN NOT NULL DEFAULT false,
    is_phone_verified BOOLEAN NOT NULL DEFAULT false,
    status TEXT,
    profile_photo_url TEXT,
    last_login_at TIMESTAMP WITH TIME ZONE,
    created_at TIMESTAMP WITH TIME ZONE NOT NULL DEFAULT NOW(),
    updated_at TIMESTAMP WITH TIME ZONE NOT NULL DEFAULT NOW()
);

CREATE INDEX IF NOT EXISTS ix_user_device_id
    ON oro_base_user(device_id);

-- =========================================================
-- 1. DOWNSTREAM TABLES (Cloud -> Edge)
-- =========================================================

CREATE TABLE IF NOT EXISTS oro_base_dog (
    dog_id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    user_id UUID NOT NULL REFERENCES oro_base_user(user_id),
    household_id UUID,
    name TEXT,
    breed TEXT,
    sex TEXT,
    date_of_birth DATE,
    weight_kg NUMERIC(5, 2),
    neutered_status BOOLEAN,
    profile_photo_url TEXT,
    notes TEXT,
    is_active BOOLEAN NOT NULL DEFAULT true,
    created_at TIMESTAMP WITH TIME ZONE NOT NULL DEFAULT NOW(),
    updated_at TIMESTAMP WITH TIME ZONE NOT NULL DEFAULT NOW()
);

CREATE INDEX IF NOT EXISTS ix_dog_user_id
    ON oro_base_dog(user_id);


CREATE TABLE IF NOT EXISTS oro_base_device (
    device_id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    household_id UUID,
    dog_id UUID REFERENCES oro_base_dog(dog_id) ON DELETE SET NULL,
    serial_number TEXT UNIQUE,
    device_name TEXT,
    model TEXT,
    firmware_version TEXT,
    hardware_version TEXT,
    status TEXT,
    settings_jsonb JSONB,
    last_seen_at TIMESTAMP WITH TIME ZONE,
    provisioned_at TIMESTAMP WITH TIME ZONE,
    is_active BOOLEAN NOT NULL DEFAULT true,
    created_at TIMESTAMP WITH TIME ZONE NOT NULL DEFAULT NOW(),
    updated_at TIMESTAMP WITH TIME ZONE NOT NULL DEFAULT NOW()
);

CREATE INDEX IF NOT EXISTS ix_device_dog_id
    ON oro_base_device(dog_id);


DO $$
BEGIN
    IF NOT EXISTS (
        SELECT 1
        FROM pg_constraint
        WHERE conname = 'fk_user_device'
    ) THEN
        ALTER TABLE oro_base_user
            ADD CONSTRAINT fk_user_device
            FOREIGN KEY (device_id)
            REFERENCES oro_base_device(device_id)
            ON DELETE SET NULL;
    END IF;
END $$;


CREATE TABLE IF NOT EXISTS oro_base_user_device_access (
    access_id UUID NOT NULL REFERENCES oro_base_user(user_id) ON DELETE CASCADE,
    device_id UUID NOT NULL REFERENCES oro_base_device(device_id) ON DELETE CASCADE,
    dog_id UUID REFERENCES oro_base_dog(dog_id) ON DELETE SET NULL,
    role TEXT,
    status TEXT,
    invited_by_user_id UUID REFERENCES oro_base_user(user_id) ON DELETE SET NULL,
    granted_at TIMESTAMP WITH TIME ZONE,
    revoked_at TIMESTAMP WITH TIME ZONE,
    created_at TIMESTAMP WITH TIME ZONE NOT NULL DEFAULT NOW(),
    updated_at TIMESTAMP WITH TIME ZONE NOT NULL DEFAULT NOW(),
    PRIMARY KEY (access_id, device_id)
);

CREATE INDEX IF NOT EXISTS ix_user_device_access_device_id
    ON oro_base_user_device_access(device_id);

CREATE INDEX IF NOT EXISTS ix_user_device_access_dog_id
    ON oro_base_user_device_access(dog_id);

CREATE INDEX IF NOT EXISTS ix_user_device_access_invited_by_user_id
    ON oro_base_user_device_access(invited_by_user_id);


CREATE TABLE IF NOT EXISTS oro_base_feeding_schedules (
    feeding_schedule_id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    device_id UUID NOT NULL REFERENCES oro_base_device(device_id) ON DELETE CASCADE,
    dog_id UUID NOT NULL REFERENCES oro_base_dog(dog_id) ON DELETE CASCADE,
    meal_name TEXT,
    scheduled_time TIME,
    timezone TEXT,
    portion_grams NUMERIC(6, 2),
    recurrence_type TEXT,
    recurrence_days JSONB,
    is_active BOOLEAN NOT NULL DEFAULT true,
    valid_from DATE,
    valid_until DATE,
    created_by_user_id UUID REFERENCES oro_base_user(user_id) ON DELETE SET NULL,
    created_at TIMESTAMP WITH TIME ZONE NOT NULL DEFAULT NOW(),
    updated_at TIMESTAMP WITH TIME ZONE NOT NULL DEFAULT NOW()
);

CREATE INDEX IF NOT EXISTS ix_feeding_schedule_device_dog
    ON oro_base_feeding_schedules(device_id, dog_id);

CREATE INDEX IF NOT EXISTS ix_feeding_schedule_created_by_user
    ON oro_base_feeding_schedules(created_by_user_id);


CREATE TABLE IF NOT EXISTS oro_base_care_schedules (
    care_schedule_id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    dog_id UUID NOT NULL REFERENCES oro_base_dog(dog_id) ON DELETE CASCADE,
    device_id UUID NOT NULL REFERENCES oro_base_device(device_id) ON DELETE CASCADE,
    care_type TEXT,
    title TEXT,
    description TEXT,
    recurrence_type TEXT,
    recurrence_days JSONB,
    scheduled_time TIME,
    due_date DATE,
    start_date DATE,
    end_date DATE,
    last_completed_at TIMESTAMP WITH TIME ZONE,
    is_active BOOLEAN NOT NULL DEFAULT true,
    created_by_user_id UUID REFERENCES oro_base_user(user_id) ON DELETE SET NULL,
    created_at TIMESTAMP WITH TIME ZONE NOT NULL DEFAULT NOW(),
    updated_at TIMESTAMP WITH TIME ZONE NOT NULL DEFAULT NOW()
);

CREATE INDEX IF NOT EXISTS ix_care_schedule_device_dog
    ON oro_base_care_schedules(device_id, dog_id);

CREATE INDEX IF NOT EXISTS ix_care_schedule_created_by_user
    ON oro_base_care_schedules(created_by_user_id);


-- =========================================================
-- 2. UPSTREAM TABLES (Edge -> Cloud)
-- =========================================================

CREATE TABLE IF NOT EXISTS oro_base_signals (
    signal_id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    device_id UUID NOT NULL REFERENCES oro_base_device(device_id) ON DELETE CASCADE,
    dog_id UUID REFERENCES oro_base_dog(dog_id) ON DELETE SET NULL,
    signal_type TEXT NOT NULL,
    signal_value_numeric NUMERIC,
    signal_value_text TEXT,
    signal_value_boolean BOOLEAN,
    unit TEXT,
    observed_at TIMESTAMP WITH TIME ZONE NOT NULL,
    ingested_at TIMESTAMP WITH TIME ZONE,
    source TEXT,
    confidence NUMERIC(5, 2) CHECK (confidence IS NULL OR (confidence >= 0 AND confidence <= 1)),
    metadata JSONB,
    created_at TIMESTAMP WITH TIME ZONE NOT NULL DEFAULT NOW(),
    sync_status TEXT NOT NULL DEFAULT 'pending'
);

CREATE INDEX IF NOT EXISTS ix_signal_device_observed
    ON oro_base_signals(device_id, observed_at);

CREATE INDEX IF NOT EXISTS ix_signal_dog_observed
    ON oro_base_signals(dog_id, observed_at);

CREATE INDEX IF NOT EXISTS ix_signal_sync_status
    ON oro_base_signals(sync_status);


CREATE TABLE IF NOT EXISTS oro_base_events (
    event_id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    device_id UUID NOT NULL REFERENCES oro_base_device(device_id) ON DELETE CASCADE,
    dog_id UUID REFERENCES oro_base_dog(dog_id) ON DELETE SET NULL,
    event_type TEXT,
    category TEXT,
    event_source TEXT,
    severity TEXT,
    status TEXT,
    trigger_mode TEXT,
    detected_at TIMESTAMP WITH TIME ZONE,
    event_start_at TIMESTAMP WITH TIME ZONE,
    event_end_at TIMESTAMP WITH TIME ZONE,
    confidence NUMERIC(5, 2) CHECK (confidence IS NULL OR (confidence >= 0 AND confidence <= 1)),
    title TEXT,
    description TEXT,
    payload JSONB,
    trigger_context JSONB,
    root_signal_refs JSONB,
    dedupe_key TEXT,
    notification_eligible BOOLEAN,
    created_at TIMESTAMP WITH TIME ZONE NOT NULL DEFAULT NOW(),
    updated_at TIMESTAMP WITH TIME ZONE NOT NULL DEFAULT NOW(),
    sync_status TEXT NOT NULL DEFAULT 'pending'
);

CREATE INDEX IF NOT EXISTS ix_event_dog_detected
    ON oro_base_events(dog_id, detected_at);

CREATE INDEX IF NOT EXISTS ix_event_device_detected
    ON oro_base_events(device_id, detected_at);

CREATE INDEX IF NOT EXISTS ix_event_sync_status
    ON oro_base_events(sync_status);


CREATE TABLE IF NOT EXISTS oro_base_notifications (
    notification_id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    device_id UUID NOT NULL REFERENCES oro_base_device(device_id) ON DELETE CASCADE,
    dog_id UUID REFERENCES oro_base_dog(dog_id) ON DELETE SET NULL,
    user_id UUID NOT NULL REFERENCES oro_base_user(user_id) ON DELETE CASCADE,
    event_id UUID,
    notification_type TEXT,
    category TEXT,
    notification_key TEXT,
    title TEXT,
    message TEXT,
    priority TEXT,
    status TEXT,
    delivery_channel TEXT,
    trigger_mode TEXT,
    scheduled_for TIMESTAMP WITH TIME ZONE,
    generated_at TIMESTAMP WITH TIME ZONE,
    delivered_at TIMESTAMP WITH TIME ZONE,
    read_at TIMESTAMP WITH TIME ZONE,
    dismissed_at TIMESTAMP WITH TIME ZONE,
    action_url TEXT,
    action_label TEXT,
    payload JSONB,
    dedupe_key TEXT,
    expires_at TIMESTAMP WITH TIME ZONE,
    created_at TIMESTAMP WITH TIME ZONE NOT NULL DEFAULT NOW(),
    updated_at TIMESTAMP WITH TIME ZONE NOT NULL DEFAULT NOW(),
    sync_status TEXT NOT NULL DEFAULT 'pending'
);

CREATE INDEX IF NOT EXISTS ix_notification_user_status
    ON oro_base_notifications(user_id, status);

CREATE INDEX IF NOT EXISTS ix_notification_expires_at
    ON oro_base_notifications(expires_at);

CREATE INDEX IF NOT EXISTS ix_notification_sync_status
    ON oro_base_notifications(sync_status);


CREATE TABLE IF NOT EXISTS oro_base_summary (
    summary_id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    device_id UUID NOT NULL REFERENCES oro_base_device(device_id) ON DELETE CASCADE,
    dog_id UUID REFERENCES oro_base_dog(dog_id) ON DELETE SET NULL,
    summary_type TEXT,
    period_start TIMESTAMP WITH TIME ZONE,
    period_end TIMESTAMP WITH TIME ZONE,
    summary_date DATE,
    status TEXT,
    title TEXT,
    payload JSONB,
    generated_at TIMESTAMP WITH TIME ZONE,
    generation_version TEXT,
    source_event_refs JSONB,
    source_signal_window JSONB,
    created_at TIMESTAMP WITH TIME ZONE NOT NULL DEFAULT NOW(),
    updated_at TIMESTAMP WITH TIME ZONE NOT NULL DEFAULT NOW(),
    sync_status TEXT NOT NULL DEFAULT 'pending'
);

CREATE INDEX IF NOT EXISTS ix_summary_dog_period
    ON oro_base_summary(dog_id, period_start);

CREATE INDEX IF NOT EXISTS ix_summary_device_type_period
    ON oro_base_summary(device_id, summary_type, period_start);

CREATE INDEX IF NOT EXISTS ix_summary_sync_status
    ON oro_base_summary(sync_status);

-- Idempotency guard: one summary per (device, type, date).
-- Duplicates are removed during cleanup; new inserts must use ON CONFLICT DO NOTHING.
CREATE UNIQUE INDEX IF NOT EXISTS uix_summary_device_type_date
    ON oro_base_summary(device_id, summary_type, summary_date)
    WHERE summary_date IS NOT NULL;


CREATE TABLE IF NOT EXISTS oro_base_context_entries (
    context_entry_id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    dog_id UUID NOT NULL REFERENCES oro_base_dog(dog_id) ON DELETE CASCADE,
    created_by_user_id UUID NOT NULL REFERENCES oro_base_user(user_id),
    source_type TEXT NOT NULL,
    context_type TEXT NOT NULL,
    title TEXT,
    text_content TEXT,
    observed_at TIMESTAMP WITH TIME ZONE,
    submitted_at TIMESTAMP WITH TIME ZONE NOT NULL DEFAULT NOW(),
    related_event_id UUID REFERENCES oro_base_events(event_id) ON DELETE SET NULL,
    related_request_id UUID,
    created_at TIMESTAMP WITH TIME ZONE NOT NULL DEFAULT NOW(),
    updated_at TIMESTAMP WITH TIME ZONE NOT NULL DEFAULT NOW(),
    sync_status TEXT NOT NULL DEFAULT 'pending',
    CONSTRAINT ck_context_entry_source_type CHECK (
        source_type IN ('user_record', 'user_question', 'system_request_response', 'vet_request_response')
    ),
    CONSTRAINT ck_context_entry_type CHECK (
        context_type IN ('text', 'image', 'video', 'mixed')
    )
);

CREATE INDEX IF NOT EXISTS ix_context_entry_dog
    ON oro_base_context_entries(dog_id);

CREATE INDEX IF NOT EXISTS ix_context_entry_created_by_user
    ON oro_base_context_entries(created_by_user_id);

CREATE INDEX IF NOT EXISTS ix_context_entry_related_event
    ON oro_base_context_entries(related_event_id);

CREATE INDEX IF NOT EXISTS ix_context_entry_related_request
    ON oro_base_context_entries(related_request_id);

CREATE INDEX IF NOT EXISTS ix_context_entry_sync_status
    ON oro_base_context_entries(sync_status);


CREATE TABLE IF NOT EXISTS oro_base_context_media_assets (
    media_asset_id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    context_entry_id UUID NOT NULL REFERENCES oro_base_context_entries(context_entry_id) ON DELETE CASCADE,
    media_type TEXT NOT NULL,
    storage_url TEXT NOT NULL,
    thumbnail_url TEXT,
    mime_type TEXT,
    uploaded_at TIMESTAMP WITH TIME ZONE NOT NULL,
    created_at TIMESTAMP WITH TIME ZONE NOT NULL DEFAULT NOW(),
    sync_status TEXT NOT NULL DEFAULT 'pending',
    CONSTRAINT ck_context_media_asset_media_type CHECK (media_type IN ('image', 'video'))
);

CREATE INDEX IF NOT EXISTS ix_context_media_asset_context_entry
    ON oro_base_context_media_assets(context_entry_id);

CREATE INDEX IF NOT EXISTS ix_context_media_asset_sync_status
    ON oro_base_context_media_assets(sync_status);


-- =========================================================
-- 3. SYNC MANAGEMENT TABLES
-- =========================================================

CREATE TABLE IF NOT EXISTS sync_watermarks (
    table_name TEXT PRIMARY KEY,
    last_synced_at TIMESTAMP WITH TIME ZONE NOT NULL DEFAULT '1970-01-01 00:00:00Z',
    updated_at TIMESTAMP WITH TIME ZONE NOT NULL DEFAULT NOW()
);

INSERT INTO sync_watermarks (table_name) VALUES
    ('oro_base_user'),
    ('oro_base_dog'),
    ('oro_base_device'),
    ('oro_base_user_device_access'),
    ('oro_base_feeding_schedules'),
    ('oro_base_care_schedules')
ON CONFLICT (table_name) DO NOTHING;


-- Local user rows now exist; ownership and access references use FKs.
COMMENT ON COLUMN oro_base_user.device_id
    IS 'Local device FK for the user profile.';

COMMENT ON COLUMN oro_base_dog.user_id
    IS 'Local user FK for the dog owner.';

COMMENT ON COLUMN oro_base_user_device_access.access_id
    IS 'Local user FK for the access row.';

COMMENT ON COLUMN oro_base_user_device_access.invited_by_user_id
    IS 'Local user FK for the inviter.';

COMMENT ON COLUMN oro_base_context_entries.created_by_user_id
    IS 'Local user FK for the context author.';

COMMENT ON COLUMN oro_base_feeding_schedules.created_by_user_id
    IS 'Local user FK for the schedule creator.';

COMMENT ON COLUMN oro_base_care_schedules.created_by_user_id
    IS 'Local user FK for the schedule creator.';

COMMENT ON COLUMN oro_base_notifications.user_id
    IS 'Local user FK for the notification recipient.';


-- =========================================================
-- 4. SCHEDULED TASK MANAGER TABLES
-- =========================================================

-- STM_JOB_LOCKS: Database-level mutual exclusion for job execution.
--
-- SAFETY MAPPING:
--   PRD §5  "Duplicate prevention" → Only one worker processes a job at a time.
--   PRD §11 "Locking"             → Database-backed, not in-memory.
--   PRD §8  "Scheduler crash"     → Locks auto-expire via locked_until TTL.
--
-- HORIZONTAL SCALING:
--   Multiple STM instances can run against the same database.
--   The INSERT ... ON CONFLICT ... WHERE locked_until < NOW() RETURNING
--   pattern ensures exactly-once acquisition per job_name within the
--   lock window. If a daemon crashes, the TTL expires and another
--   instance can reclaim the lock without manual intervention.
--
-- C++ BINDING (stm_lock_acquire):
--   INSERT INTO stm_job_locks (job_name, lock_key, locked_until, owner)
--   VALUES ($1, $2, NOW() + ($3 || ' seconds')::interval, $4)
--   ON CONFLICT (job_name) DO UPDATE
--     SET lock_key = EXCLUDED.lock_key, locked_at = NOW(),
--         locked_until = EXCLUDED.locked_until, owner = EXCLUDED.owner
--     WHERE stm_job_locks.locked_until < NOW()
--   RETURNING job_name;
--
-- IDEMPOTENCY: If locked_until is still in the future AND the current
-- owner differs, the RETURNING clause yields 0 rows → lock not acquired.
CREATE TABLE IF NOT EXISTS stm_job_locks (
    job_name     TEXT PRIMARY KEY,
    lock_key     TEXT NOT NULL,
    locked_at    TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    locked_until TIMESTAMPTZ NOT NULL,
    owner        TEXT NOT NULL
);

CREATE INDEX IF NOT EXISTS ix_stm_locks_expiry
    ON stm_job_locks(locked_until);


-- STM_JOB_EXECUTIONS: Immutable execution history / audit log.
--
-- SAFETY MAPPING:
--   PRD §10 "Job success/failure count" → COUNT(*) GROUP BY status
--   PRD §10 "Job duration"              → duration_ms column
--   PRD §10 "Retry count"              → Filter by retry_attempt > 0
--   PRD §11 "Audit logs"               → Every execution is recorded
--
-- RETENTION: Cleaned by data_cleanup job using configured retention_days.
CREATE TABLE IF NOT EXISTS stm_job_executions (
    execution_id    UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    job_name        TEXT NOT NULL,
    status          TEXT NOT NULL CHECK (status IN (
                        'completed', 'failed', 'skipped', 'timeout')),
    started_at      TIMESTAMPTZ NOT NULL,
    finished_at     TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    duration_ms     INTEGER NOT NULL,
    items_processed INTEGER NOT NULL DEFAULT 0,
    error           TEXT,
    metadata        JSONB,
    owner           TEXT NOT NULL,
    retry_attempt   INTEGER NOT NULL DEFAULT 0,
    created_at      TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE INDEX IF NOT EXISTS ix_stm_exec_job
    ON stm_job_executions(job_name, started_at DESC);

CREATE INDEX IF NOT EXISTS ix_stm_exec_status
    ON stm_job_executions(status);


-- STM_RETRY_QUEUE: Failed jobs queued for retry with exponential backoff.
--
-- SAFETY MAPPING:
--   PRD §5 "Retries"            → Exponential backoff via next_retry_at
--   PRD §8 "Repeated failures"  → max_attempts, then status = 'dead_letter'
--   PRD §9 "View dead-letter"   → WHERE status = 'dead_letter'
--
-- BACKOFF FORMULA (computed in C++):
--   next_retry_at = NOW() + min(base_delay * 2^attempt, 600s)
--   base_delay = 30 seconds
--
-- NOTE: Table is provisioned now but only actively populated once
-- the retry engine is implemented in a future phase.
CREATE TABLE IF NOT EXISTS stm_retry_queue (
    retry_id      UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    job_name      TEXT NOT NULL,
    payload       JSONB,
    attempts      INTEGER NOT NULL DEFAULT 0,
    max_attempts  INTEGER NOT NULL DEFAULT 3,
    next_retry_at TIMESTAMPTZ NOT NULL,
    status        TEXT NOT NULL DEFAULT 'pending'
                  CHECK (status IN ('pending', 'retrying', 'dead_letter')),
    last_error    TEXT,
    created_at    TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at    TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE INDEX IF NOT EXISTS ix_stm_retry_pending
    ON stm_retry_queue(status, next_retry_at)
    WHERE status = 'pending';