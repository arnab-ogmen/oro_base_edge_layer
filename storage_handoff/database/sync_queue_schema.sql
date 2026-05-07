PRAGMA foreign_keys = ON;

CREATE TABLE IF NOT EXISTS sync_log (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    batch_id TEXT,
    idempotency_key TEXT UNIQUE NOT NULL,
    table_name TEXT NOT NULL,
    operation TEXT NOT NULL CHECK(operation IN ('INSERT','UPDATE','DELETE')),
    payload TEXT NOT NULL,
    checksum TEXT NOT NULL,
    lsn INTEGER NOT NULL,
    status TEXT DEFAULT 'pending' CHECK(status IN ('pending','in_progress','synced','failed','dead')),
    attempt INTEGER DEFAULT 0,
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
    synced_at DATETIME
);

CREATE TABLE IF NOT EXISTS checkpoint (
    device_id TEXT PRIMARY KEY,
    last_lsn INTEGER NOT NULL,
    updated_at DATETIME DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS sync_jobs (
    job_id TEXT PRIMARY KEY,
    batch_id TEXT NOT NULL,
    status TEXT DEFAULT 'pending' CHECK(status IN ('pending','in_progress','acked','completed','failed','retry_scheduled')),
    record_count INTEGER,
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
    updated_at DATETIME
);

CREATE TABLE IF NOT EXISTS media_uploads (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    object_key TEXT UNIQUE NOT NULL,
    record_id INTEGER REFERENCES sync_log(id),
    status TEXT DEFAULT 'initiated' CHECK(status IN ('initiated','uploading','uploaded','linked','failed')),
    s3_url TEXT,
    initiated_at DATETIME DEFAULT CURRENT_TIMESTAMP,
    completed_at DATETIME
);

CREATE TABLE IF NOT EXISTS dead_letter (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    sync_log_id INTEGER REFERENCES sync_log(id),
    reason TEXT,
    payload TEXT,
    failed_at DATETIME DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX IF NOT EXISTS idx_sync_log_status_lsn ON sync_log(status, lsn, id);
CREATE INDEX IF NOT EXISTS idx_sync_log_batch_id ON sync_log(batch_id);
CREATE INDEX IF NOT EXISTS idx_media_uploads_status ON media_uploads(status, initiated_at);
CREATE INDEX IF NOT EXISTS idx_sync_jobs_status ON sync_jobs(status, created_at);