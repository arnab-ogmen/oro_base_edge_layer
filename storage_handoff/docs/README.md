# storage_handoff — Integration Guide

`storage_handoff` is a modular C++17 **shared library** that provides a generic PostgreSQL write interface for any ORo Base edge-layer package. Install it once system-wide (or to a local prefix), then any node can link against it with a single `find_package` call — no relative paths, no source duplication.

---

## Table of Contents

1. [Overview](#overview)
2. [Directory Structure](#directory-structure)
3. [Building and Installing the Library](#building-and-installing-the-library)
   - [Quick install (script)](#quick-install-script)
   - [Manual install](#manual-install)
   - [Custom install prefix](#custom-install-prefix)
4. [Public API](#public-api)
   - [StorageWriter](#storagewriter)
   - [unix_ms_to_iso8601](#unix_ms_to_iso8601)
5. [Integrating into a Package](#integrating-into-a-package)
   - [Step 1 — CMakeLists.txt](#step-1--cmakeliststxt)
   - [Step 2 — Include the Header](#step-2--include-the-header)
   - [Step 3 — Construct the Writer](#step-3--construct-the-writer)
   - [Step 4 — Register SQL Statements](#step-4--register-sql-statements)
   - [Step 5 — Execute Writes](#step-5--execute-writes)
6. [Full Worked Example](#full-worked-example)
7. [Database Setup](#database-setup)
8. [Connection String Reference](#connection-string-reference)
9. [Error Handling & Reconnection](#error-handling--reconnection)
10. [Design Notes](#design-notes)

---

## Overview

`storage_handoff` wraps `libpqxx` (the official C++ PostgreSQL client) and provides:

- A **single-connection PostgreSQL writer** with automatic reconnection.
- A **prepare/execute** model: SQL statements are registered by name once at startup, then called cheaply by name on every write.
- A **static time utility** (`unix_ms_to_iso8601`) for converting Unix epoch milliseconds to ISO 8601 timestamp strings required by PostgreSQL `TIMESTAMP WITH TIME ZONE` columns.

The library intentionally contains **no domain logic**. It knows nothing about signals, commands, events, or any specific table. The caller owns all SQL.

---

## Directory Structure

```
storage_handoff/
├── CMakeLists.txt              ← builds libstorage_handoff.so + install rules
├── cmake/
│   └── storage_handoffConfig.cmake.in  ← find_package() config template
├── docs/
│   └── README.md               ← you are here
├── database/
│   ├── schema.sql              ← full edge DB schema (all tables)
│   └── sync_queue_schema.sql
├── include/
│   └── storage_handoff/
│       └── storage_handoff.hpp ← public header (the only file callers include)
├── install.sh                  ← one-shot build + install convenience script
├── setup_local_postgres.sh     ← one-shot DB setup script
└── src/
    └── storage_handoff.cpp
```

---

## Building and Installing the Library

> **Install once. Link everywhere.** After installation, any package on the system can use `find_package(storage_handoff REQUIRED)` — no relative paths needed.

### Quick install (script)

```bash
cd oro_base_edge_layer/storage_handoff
chmod +x install.sh

# Install to /usr/local (requires sudo)
sudo ./install.sh

# OR: install to a user-local prefix (no sudo needed)
./install.sh /home/$USER/.local
```

The script builds in Release mode, installs the `.so`, headers, and CMake config files, and runs `ldconfig`.

---

### Manual install

```bash
cd oro_base_edge_layer/storage_handoff
mkdir -p build_install

cmake -S . -B build_install \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr/local

cmake --build build_install --parallel $(nproc)

sudo cmake --install build_install
sudo ldconfig
```

---

### Custom install prefix

If you do not have `sudo` access, install to a user-writable location:

```bash
cmake -S . -B build_install \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/home/$USER/.local

cmake --build build_install --parallel $(nproc)
cmake --install build_install
```

When building consumer packages against a custom prefix, pass it via `-DCMAKE_PREFIX_PATH`:

```bash
cmake -S . -B build \
    -DCMAKE_PREFIX_PATH=/home/$USER/.local
```

After installation the following files are placed on the system:

| File | Purpose |
|---|---|
| `$PREFIX/lib/libstorage_handoff.so` | Main shared library (symlink) |
| `$PREFIX/lib/libstorage_handoff.so.1` | SOVERSION symlink |
| `$PREFIX/lib/libstorage_handoff.so.1.0.0` | Fully versioned library |
| `$PREFIX/include/storage_handoff/storage_handoff.hpp` | Public header |
| `$PREFIX/lib/cmake/storage_handoff/` | CMake package config files |

---

## Public API

### `StorageWriter`

```cpp
namespace storage_handoff {

class StorageWriter {
public:
    // Construct with a PostgreSQL connection string.
    // Attempts to connect immediately (non-blocking on failure).
    explicit StorageWriter(const std::string &conn_str);

    // Register a named prepared statement.
    // Call this once at startup, before any writes.
    void prepare(const std::string &name, const std::string &query);

    // Execute a previously registered prepared statement with arbitrary arguments.
    // Returns true on success, false on any DB error.
    template <typename... Args>
    bool execute_prepared(const std::string &stmt_name, Args &&...args);

    // Convert Unix epoch milliseconds to ISO 8601 string.
    // Suitable for PostgreSQL TIMESTAMP WITH TIME ZONE columns.
    static std::string unix_ms_to_iso8601(uint64_t unix_ms);
};

} // namespace storage_handoff
```

**Key behaviours:**
- If the constructor fails to connect (e.g., DB is not running at startup), the node still starts. The next write will retry the connection.
- Reconnect is **throttled to once every 5 seconds** to avoid log spam.
- After a reconnect, all previously registered prepared statements are automatically re-registered on the new connection.

---

### `unix_ms_to_iso8601`

```cpp
// Input:  1715000000123  (Unix epoch milliseconds)
// Output: "2024-05-06T19:33:20.123Z"
std::string ts = storage_handoff::StorageWriter::unix_ms_to_iso8601(unix_ms);
```

This is a `static` method — callable without a `StorageWriter` instance, useful in helper functions that build SQL arguments.

---

## Integrating into a Package

### Step 1 — CMakeLists.txt

After installing the library, add a single `find_package` call. No relative paths, no `add_subdirectory`.

```cmake
cmake_minimum_required(VERSION 3.15)
project(my_node VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Locate the installed storage_handoff shared library
find_package(storage_handoff REQUIRED)

add_executable(my_node
    src/main.cpp
    src/my_logic.cpp
)

# Link via the namespaced imported target.
# This also transitively links pqxx and pq — nothing else needed.
target_link_libraries(my_node
    PRIVATE
    storage_handoff::storage_handoff
)
```

> **Tip:** If you installed to a custom prefix (e.g., `~/.local`), pass it at configure time:
> ```bash
> cmake -S . -B build -DCMAKE_PREFIX_PATH=/home/$USER/.local
> ```

---

### Step 2 — Include the Header

```cpp
#include "storage_handoff/storage_handoff.hpp"
```

---

### Step 3 — Construct the Writer

Construct one `StorageWriter` per process, typically in `main`. Pass the full PostgreSQL connection string:

```cpp
const std::string conn_str =
    "host=localhost user=oro_user password=<password> dbname=oro_base_db";

storage_handoff::StorageWriter writer(conn_str);
```

**Connection string format** (libpq keyword-value syntax):

| Keyword    | Example value     | Description                          |
|------------|-------------------|--------------------------------------|
| `host`     | `localhost`       | Hostname or IP of the PostgreSQL server |
| `port`     | `5432`            | Port (default 5432)                  |
| `user`     | `oro_user`        | PostgreSQL role name                 |
| `password` | `<password>`      | Role password                        |
| `dbname`   | `oro_base_db`     | Target database name                 |

---

### Step 4 — Register SQL Statements

After constructing the writer, register each SQL query your package will use. Use `$1`, `$2`, ... as positional placeholders:

```cpp
writer.prepare("insert_my_event", R"(
    INSERT INTO public.oro_base_events (
        device_id, dog_id, event_type, category,
        detected_at, payload, created_at
    )
    VALUES ($1, $2, $3, $4, $5, $6::jsonb, NOW())
)");
```

- **Name** is an arbitrary string you choose — unique within this connection.
- **Placeholders** are 1-indexed: `$1` = first argument, `$2` = second, etc.
- You can register as many statements as needed.
- Call `prepare()` **before** the node enters its main loop.

---

### Step 5 — Execute Writes

Call `execute_prepared` with the statement name followed by arguments in the same order as your SQL placeholders:

```cpp
std::string ts = storage_handoff::StorageWriter::unix_ms_to_iso8601(unix_ms);

bool ok = writer.execute_prepared(
    "insert_my_event",
    device_id,           // $1 — std::string (UUID)
    std::nullopt,        // $2 — std::optional<std::string> (nullable dog_id)
    "feeding_complete",  // $3 — std::string (event_type)
    "feeding",           // $4 — std::string (category)
    ts,                  // $5 — std::string (ISO 8601 timestamp)
    R"({"portion_g": 42})"  // $6 — JSON string (cast to ::jsonb in SQL)
);

if (!ok) {
    std::cerr << "[MyNode] Failed to write event to DB\n";
}
```

**Type mapping:**

| C++ type                   | PostgreSQL type                        |
|----------------------------|----------------------------------------|
| `std::string`              | `TEXT`, `UUID`, `TIMESTAMP`, `JSONB`   |
| `double`                   | `NUMERIC`, `FLOAT8`                    |
| `int`, `int64_t`           | `INTEGER`, `BIGINT`                    |
| `bool`                     | `BOOLEAN`                              |
| `std::optional<T>`         | nullable version of any type above     |
| `std::nullopt`             | `NULL`                                 |

> **JSONB columns:** pass your JSON as a `std::string` and cast in SQL: `$N::jsonb`.

---

## Full Worked Example

A complete package that writes feeding events using the installed shared library.

**`CMakeLists.txt`:**
```cmake
cmake_minimum_required(VERSION 3.15)
project(feeding_event_node VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(storage_handoff REQUIRED)

add_executable(feeding_event_node src/main.cpp)

target_link_libraries(feeding_event_node
    PRIVATE
    storage_handoff::storage_handoff
)
```

**Build:**
```bash
cmake -S . -B build
# If using a custom prefix:
# cmake -S . -B build -DCMAKE_PREFIX_PATH=/home/$USER/.local
cmake --build build
```

**`src/main.cpp`:**
```cpp
#include "storage_handoff/storage_handoff.hpp"
#include <chrono>
#include <iostream>

static uint64_t now_ms() {
    auto now = std::chrono::system_clock::now();
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()).count());
}

int main() {
    // 1. Connect
    const std::string conn_str =
        "host=localhost user=oro_user password=ogmen dbname=oro_base_db";
    storage_handoff::StorageWriter writer(conn_str);

    // 2. Register SQL
    writer.prepare("insert_feeding_event", R"(
        INSERT INTO public.oro_base_events (
            device_id, dog_id, event_type, category, severity,
            detected_at, title, payload, created_at
        )
        VALUES ($1, $2, $3, $4, $5, $6, $7, $8::jsonb, NOW())
    )");

    // 3. Write
    std::string device_id = "550e8400-e29b-41d4-a716-446655440000";
    std::optional<std::string> dog_id = std::nullopt;
    std::string ts = storage_handoff::StorageWriter::unix_ms_to_iso8601(now_ms());

    bool ok = writer.execute_prepared(
        "insert_feeding_event",
        device_id,           // $1
        dog_id,              // $2 (NULL)
        "feeding_complete",  // $3
        "feeding",           // $4
        "info",              // $5
        ts,                  // $6
        "Feeding dispensed", // $7
        R"({"portion_g": 42, "bowl_weight_g": 150})"  // $8
    );

    std::cout << (ok ? "✅ Written" : "❌ Failed") << "\n";
    return ok ? 0 : 1;
}
```

---

## Database Setup

Run the one-shot setup script to create the `oro_base_db` database, the `oro_user` role, and apply the full schema:

```bash
cd oro_base_edge_layer/storage_handoff
chmod +x setup_local_postgres.sh
./setup_local_postgres.sh
```

To apply the schema manually to an existing database:

```bash
psql -U oro_user -d oro_base_db -f database/schema.sql
```

---

## Connection String Reference

The connection string follows the standard **libpq keyword-value** format. All parameters can be overridden by environment variables:

| Environment Variable | Overrides keyword |
|----------------------|-------------------|
| `PGHOST`             | `host`            |
| `PGPORT`             | `port`            |
| `PGUSER`             | `user`            |
| `PGPASSWORD`         | `password`        |
| `PGDATABASE`         | `dbname`          |

Using environment variables is recommended for production to avoid hardcoding credentials:

```bash
export PGHOST=localhost
export PGUSER=oro_user
export PGPASSWORD=ogmen
export PGDATABASE=oro_base_db
```

Then construct with an empty string and libpq reads from the environment:
```cpp
storage_handoff::StorageWriter writer("");
```

---

## Error Handling & Reconnection

`execute_prepared` returns `bool`:

- `true` — the transaction was committed successfully.
- `false` — a DB error occurred. Details are printed to `stderr`. The write is **not** retried automatically.

Reconnection is transparent inside `ensure_connection()`:
- If the connection is lost, the next `execute_prepared` call attempts a reconnect.
- Reconnect is **throttled to once every 5 seconds**.
- All previously registered prepared statements are automatically re-registered after reconnect.

Caller responsibilities:
- **Register all statements before the main loop.** `prepare()` should only be called during initialization.
- **Handle `false` returns** if write loss is not tolerable — e.g., enqueue to a local fallback, emit a health signal, or increment a counter.

---

## Design Notes

- **Shared library, not static:** Builds as `libstorage_handoff.so`. Changes to the library (bug fixes, new features) can be deployed without recompiling consumer packages, as long as the major version (`SOVERSION`) stays the same.
- **Thread safety:** `StorageWriter` is **not thread-safe**. If multiple threads must write concurrently, each thread should own its own `StorageWriter` instance (and its own DB connection).
- **One connection per node:** The intended pattern is one `StorageWriter` per process, constructed in `main`, passed by reference to all subsystems that need to write.
- **No ORM, no reflection:** The caller constructs all SQL. This keeps compile times low and gives full control over the query plan.
- **Template methods stay in the header:** `execute_prepared` is a variadic template and is therefore defined in `storage_handoff.hpp`. It is instantiated in the consumer, not compiled into the `.so`. This is standard C++ practice for template libraries.
