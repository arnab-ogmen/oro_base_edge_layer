#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="${PROJECT_DIR:-$SCRIPT_DIR}"
WORKSPACE_ROOT="${WORKSPACE_ROOT:-$(cd "$SCRIPT_DIR/.." && pwd)}"
VENV_DIR="${VENV_DIR:-$PROJECT_DIR/venv}"
SERVER_DIR="$PROJECT_DIR"
REQUIREMENTS_FILE="$SERVER_DIR/requirements.txt"
SCHEMA_FILE="$PROJECT_DIR/database/schema.sql"
SYNC_SCHEMA_FILE="$PROJECT_DIR/database/sync_queue_schema.sql"
SYNC_SQLITE_PATH="${SYNC_SQLITE_PATH:-$PROJECT_DIR/sync_queue.db}"

DB_HOST="${DB_HOST:-localhost}"
DB_PORT="${DB_PORT:-5432}"
DB_NAME="${DB_NAME:-oro_base_db}"
DB_USER="${DB_USER:-oro_user}"
DB_PASSWORD="${DB_PASSWORD:-ogmen}"
DB_SUPERUSER="${DB_SUPERUSER:-postgres}"
DB_SUPERUSER_DB="${DB_SUPERUSER_DB:-postgres}"

log() {
  printf '%s\n' "$*"
}

die() {
  printf 'Error: %s\n' "$*" >&2
  exit 1
}

sql_literal() {
  local value="${1//\'/\'\'}"
  printf "'%s'" "$value"
}

sql_ident() {
  local value="${1//\"/\"\"}"
  printf '"%s"' "$value"
}

require_command() {
  command -v "$1" >/dev/null 2>&1 || die "Missing required command: $1"
}

ensure_postgres_ready() {
  if pg_isready -h "$DB_HOST" -p "$DB_PORT" >/dev/null 2>&1; then
    return
  fi

  if command -v sudo >/dev/null 2>&1; then
    if command -v systemctl >/dev/null 2>&1; then
      sudo systemctl start postgresql >/dev/null 2>&1 || true
    elif command -v service >/dev/null 2>&1; then
      sudo service postgresql start >/dev/null 2>&1 || true
    fi
  fi

  for _ in $(seq 1 30); do
    if pg_isready -h "$DB_HOST" -p "$DB_PORT" >/dev/null 2>&1; then
      return
    fi
    sleep 1
  done

  die "PostgreSQL is not reachable on $DB_HOST:$DB_PORT"
}

restart_postgres_service() {
  if command -v sudo >/dev/null 2>&1; then
    if command -v systemctl >/dev/null 2>&1; then
      sudo systemctl restart postgresql >/dev/null 2>&1 || true
    elif command -v service >/dev/null 2>&1; then
      sudo service postgresql restart >/dev/null 2>&1 || true
    else
      die "Unable to restart PostgreSQL automatically; restart the PostgreSQL service manually after setup"
    fi
  else
    die "Unable to restart PostgreSQL automatically; restart the PostgreSQL service manually after setup"
  fi

  for _ in $(seq 1 30); do
    if pg_isready -h "$DB_HOST" -p "$DB_PORT" >/dev/null 2>&1; then
      return
    fi
    sleep 1
  done

  die "PostgreSQL did not come back online after restart on $DB_HOST:$DB_PORT"
}

psql_super_db() {
  if [[ -n "${DB_SUPERUSER_PASSWORD:-}" ]]; then
    PGPASSWORD="$DB_SUPERUSER_PASSWORD" psql -v ON_ERROR_STOP=1 -h "$DB_HOST" -p "$DB_PORT" -U "$DB_SUPERUSER" -d "$DB_SUPERUSER_DB" "$@"
    return
  fi

  if command -v sudo >/dev/null 2>&1; then
    (cd /tmp && sudo -u postgres psql -v ON_ERROR_STOP=1 -d "$DB_SUPERUSER_DB" "$@")
    return
  fi

  psql -v ON_ERROR_STOP=1 -h "$DB_HOST" -p "$DB_PORT" -U "$DB_SUPERUSER" -d "$DB_SUPERUSER_DB" "$@"
}

psql_super_appdb() {
  if [[ -n "${DB_SUPERUSER_PASSWORD:-}" ]]; then
    PGPASSWORD="$DB_SUPERUSER_PASSWORD" psql -v ON_ERROR_STOP=1 -h "$DB_HOST" -p "$DB_PORT" -U "$DB_SUPERUSER" -d "$DB_NAME" "$@"
    return
  fi

  if command -v sudo >/dev/null 2>&1; then
    (cd /tmp && sudo -u postgres psql -v ON_ERROR_STOP=1 -d "$DB_NAME" "$@")
    return
  fi

  psql -v ON_ERROR_STOP=1 -h "$DB_HOST" -p "$DB_PORT" -U "$DB_SUPERUSER" -d "$DB_NAME" "$@"
}

psql_app() {
  PGPASSWORD="$DB_PASSWORD" psql -v ON_ERROR_STOP=1 -h "$DB_HOST" -p "$DB_PORT" -U "$DB_USER" -d "$DB_NAME" "$@"
}

ensure_venv() {
  if [[ ! -x "$VENV_DIR/bin/python" ]]; then
    log "Creating virtual environment in $VENV_DIR"
    python3 -m venv "$VENV_DIR"
  fi

  log "Upgrading pip tooling"
  "$VENV_DIR/bin/python" -m pip install --upgrade pip setuptools wheel

  log "Installing Python requirements"
  "$VENV_DIR/bin/pip" install -r "$REQUIREMENTS_FILE"

  if [[ "${INSTALL_DEV_REQUIREMENTS:-0}" == "1" ]]; then
    local dev_requirements="$SERVER_DIR/requirements-dev.txt"
    if [[ -f "$dev_requirements" ]]; then
      log "Installing dev requirements"
      "$VENV_DIR/bin/pip" install -r "$dev_requirements"
    fi
  fi

  "$VENV_DIR/bin/python" - <<'PY'
import alembic
import psycopg2
import sqlalchemy

print("Python dependency check passed.")
PY
}

ensure_role_and_database() {
  local role_exists database_exists
  local quoted_db_name quoted_db_name_ident quoted_db_user_literal quoted_db_user_ident quoted_db_password

  quoted_db_name="$(sql_literal "$DB_NAME")"
  quoted_db_name_ident="$(sql_ident "$DB_NAME")"
  quoted_db_user_literal="$(sql_literal "$DB_USER")"
  quoted_db_user_ident="$(sql_ident "$DB_USER")"
  quoted_db_password="$(sql_literal "$DB_PASSWORD")"

  role_exists="$(psql_super_db -tAc "SELECT 1 FROM pg_roles WHERE rolname = $quoted_db_user_literal" | tr -d '[:space:]')"
  if [[ "$role_exists" != "1" ]]; then
    log "Creating PostgreSQL role $DB_USER"
    psql_super_db -c "CREATE ROLE $quoted_db_user_ident LOGIN REPLICATION PASSWORD $quoted_db_password;"
  fi

  log "Ensuring replication privilege exists for $DB_USER"
  psql_super_db -c "ALTER ROLE $quoted_db_user_ident WITH REPLICATION;"

  database_exists="$(psql_super_db -tAc "SELECT 1 FROM pg_database WHERE datname = $quoted_db_name" | tr -d '[:space:]')"
  if [[ "$database_exists" != "1" ]]; then
    log "Creating PostgreSQL database $DB_NAME"
    psql_super_db -c "CREATE DATABASE $quoted_db_name_ident OWNER $quoted_db_user_ident;"
  else
    log "Updating ownership for database $DB_NAME"
    psql_super_db -c "ALTER DATABASE $quoted_db_name_ident OWNER TO $quoted_db_user_ident;"
  fi

  log "Ensuring pgcrypto extension exists"
  psql_super_appdb -c 'CREATE EXTENSION IF NOT EXISTS "pgcrypto";'
}

ensure_logical_replication_config() {
  local wal_level replication_slots wal_senders

  wal_level="$(psql_super_db -tAc "SHOW wal_level;" | tr -d '[:space:]')"
  replication_slots="$(psql_super_db -tAc "SHOW max_replication_slots;" | tr -d '[:space:]')"
  wal_senders="$(psql_super_db -tAc "SHOW max_wal_senders;" | tr -d '[:space:]')"

  if [[ "$wal_level" != "logical" || "${replication_slots:-0}" -lt 1 || "${wal_senders:-0}" -lt 1 ]]; then
    log "Configuring PostgreSQL for logical replication"
    psql_super_db -c "ALTER SYSTEM SET wal_level = 'logical';"
    psql_super_db -c "ALTER SYSTEM SET max_replication_slots = '10';"
    psql_super_db -c "ALTER SYSTEM SET max_wal_senders = '10';"
    restart_postgres_service
  fi

  wal_level="$(psql_super_db -tAc "SHOW wal_level;" | tr -d '[:space:]')"
  if [[ "$wal_level" != "logical" ]]; then
    die "PostgreSQL wal_level is still '$wal_level'; it must be 'logical' for the sync manager"
  fi
}

ensure_logical_publication() {
  local publication_name quoted_publication publication_exists table_clause table_name
  local publication_tables=(
    oro_base_user
    oro_base_dog
    oro_base_device
    oro_base_user_device_access
    oro_base_feeding_schedules
    oro_base_care_schedules
    oro_base_signals
    oro_base_events
    oro_base_notifications
    oro_base_summary
    oro_base_context_entries
    oro_base_context_media_assets
  )

  publication_name="${ORO_WAL_PUBLICATION:-oro_publication}"
  quoted_publication="$(sql_ident "$publication_name")"
  table_clause=""

  for table_name in "${publication_tables[@]}"; do
    if [[ -n "$table_clause" ]]; then
      table_clause+=", "
    fi
    table_clause+="$(sql_ident "$table_name")"
  done

  log "Ensuring logical replication publication $publication_name exists"
  publication_exists="$(psql_app -tAc "SELECT 1 FROM pg_publication WHERE pubname = $(sql_literal "$publication_name")" | tr -d '[:space:]')"
  if [[ "$publication_exists" != "1" ]]; then
    psql_app -c "CREATE PUBLICATION $quoted_publication FOR TABLE $table_clause;"
  fi
}

load_schema() {
  [[ -f "$SCHEMA_FILE" ]] || die "Schema file not found: $SCHEMA_FILE"

  log "Applying schema from $SCHEMA_FILE"
  PGPASSWORD="$DB_PASSWORD" psql -v ON_ERROR_STOP=1 -h "$DB_HOST" -p "$DB_PORT" -U "$DB_USER" -d "$DB_NAME" -f "$SCHEMA_FILE"
}

load_sync_sqlite_schema() {
  [[ -f "$SYNC_SCHEMA_FILE" ]] || die "SQLite sync schema file not found: $SYNC_SCHEMA_FILE"

  log "Applying SQLite sync schema from $SYNC_SCHEMA_FILE"
  "$VENV_DIR/bin/python" - "$SYNC_SQLITE_PATH" "$SYNC_SCHEMA_FILE" <<'PY'
import sqlite3
import sys
from pathlib import Path

sqlite_path = Path(sys.argv[1]).expanduser()
schema_path = Path(sys.argv[2]).expanduser()

sqlite_path.parent.mkdir(parents=True, exist_ok=True)

connection = sqlite3.connect(sqlite_path)
try:
    connection.execute("PRAGMA journal_mode=WAL")
    connection.execute("PRAGMA synchronous=NORMAL")
    connection.execute("PRAGMA foreign_keys=ON")
    connection.execute("PRAGMA busy_timeout=5000")
    connection.executescript(schema_path.read_text(encoding="utf-8"))
    connection.commit()
finally:
    connection.close()

print(f"Initialized SQLite sync database at {sqlite_path}")
PY
}

main() {
  require_command python3
  require_command psql
  require_command pg_isready

  [[ -f "$REQUIREMENTS_FILE" ]] || die "Requirements file not found: $REQUIREMENTS_FILE"
  [[ -f "$SCHEMA_FILE" ]] || die "Schema file not found: $SCHEMA_FILE"

  ensure_postgres_ready
  ensure_venv
  ensure_role_and_database
  ensure_logical_replication_config
  load_schema
  ensure_logical_publication
  load_sync_sqlite_schema

  log "Done"
  log "Activate the environment with: source $VENV_DIR/bin/activate"
  log "Database URL: postgresql+psycopg2://$DB_USER:$DB_PASSWORD@$DB_HOST:$DB_PORT/$DB_NAME"
  log "SQLite sync DB: $SYNC_SQLITE_PATH"
}

main "$@"