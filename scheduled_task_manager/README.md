# Scheduled Task Manager

> Non-blocking, data-driven background job scheduler for the ORo Base edge layer.

The **Scheduled Task Manager (STM)** is a monolithic service running on the Radxa SBC that periodically manages background jobs. It evaluates care schedules, monitors device and sensor health, processes aggregates for pet activity summaries, cleans up database and media assets, and retries failed actions—all running concurrently in a non-blocking architecture.

---

## 1. Overview of the Package

The Scheduled Task Manager functions as the "brain" for recurring edge operations within the `oro_base` system. It interacts with the rest of the edge layer through two key interfaces:
- **Database (`storage_handoff`)**: Executes real-time SQL queries against PostgreSQL (`oro_base_db`) to evaluate device status, compute pet summary aggregates, and track active care/feeding tasks.
- **IPC Bus (`libzmq`)**: Publishes messages/commands to the `command_executor` node via a ZeroMQ `PUSH` socket (`ipc:///tmp/oro_cmd_exec.ipc`) to trigger physical hardware actions (e.g., dispensing food, triggering LED indicator rings).

### Core Features
- **Concurrently Managed Tasks**: Periodically runs 11 specific edge-level jobs.
- **Database-Backed Job Locking**: Uses unique database-level advisory locks with customizable TTLs to ensure that jobs do not run multiple instances concurrently in clustered or restarted environments.
- **Fail-Safe Retries**: Implements exponential backoff retry scheduling for failed jobs, logging them to a persistent retry table.
- **Auditable Executions**: Writes every single job run, start time, duration, status, and target items processed into the database for transparent system monitoring.

---

## 2. Package Structure

The package is built with modularity in mind. Each category of jobs is grouped into its own domain file, while the scheduling engine handles coordination.

### Directory Layout
```
scheduled_task_manager/
├── CMakeLists.txt                  # Build configuration linking libstorage_handoff & libzmq
├── scheduled_task_manager.service   # Systemd service unit file
├── README.md                       # Package documentation
├── database/
│   └── scheduler_schema.sql        # Database tables for locks, executions, and retries
├── include/scheduled_task_manager/
│   ├── job.hpp                     # Definition of JobDefinition, JobResult, and JobCategory
│   ├── job_registry.hpp            # Main catalog of all registered jobs
│   ├── job_executor.hpp            # Handles run orchestration and command dispatching
│   ├── job_lock.hpp                # DB-backed mutual exclusion locks
│   ├── retry_queue.hpp             # Exponential backoff scheduling queue
│   ├── scheduler_engine.hpp        # Concurrency engine (Tick loop + Worker threads)
│   ├── scheduler_config.hpp        # JSON configuration wrapper
│   └── jobs/                       # Job headers grouped by domain
│       ├── care_jobs.hpp
│       ├── cleanup_jobs.hpp
│       ├── device_jobs.hpp
│       ├── health_jobs.hpp
│       ├── retry_jobs.hpp
│       └── summary_jobs.hpp
└── src/
    ├── main.cpp                    # Node entry point, signals handler, and bootstrap
    ├── job_registry.cpp            # Job registration mappings
    ├── job_executor.cpp            # Command dispatch and execution logging
    ├── job_lock.cpp                # Advisory locking implementation
    ├── retry_queue.cpp             # Retries management logic
    ├── scheduler_engine.cpp        # Work distribution loop
    ├── scheduler_config.cpp        # Config parser
    └── jobs/                       # Job database query definitions
        ├── care_jobs.cpp           # Feeding schedules overdue detection
        ├── cleanup_jobs.cpp        # DB history truncation and expired alerts purge
        ├── device_jobs.cpp         # Battery level monitoring and sensor staleness detection
        ├── health_jobs.cpp         # Activity evaluation and consumption baseline calculation
        ├── retry_jobs.cpp          # Sync retries triggers (stubs)
        └── summary_jobs.cpp        # Daily/weekly summary aggregators
```

### Database Schema Mappings
The STM utilizes three dedicated tables in `oro_base_db` (defined in `database/scheduler_schema.sql`):
1. **`stm_job_locks`**: Manages unique `job_name` records, locking timestamps (`locked_at`), expiration (`locked_until`), and the hostname-process `owner` to enforce single-node concurrency.
2. **`stm_job_executions`**: Tracks every execution record containing `started_at`, `finished_at`, `duration_ms`, `status` (`completed` or `failed`), `items_processed`, and error logs.
3. **`stm_retry_queue`**: Stores deferred jobs with backoff schedules (`next_retry_at`), attempt counts, and serialized JSON payloads.

---

## 3. Execution Logic

The execution architecture separates scheduling, queueing, and execution to ensure the service remains responsive under heavy database load.

```mermaid
graph TB
    subgraph STM ["Scheduled Task Manager Node"]
        TICK["Tick Loop (1s Interval)"]
        PQ["Priority Work Queue (Min-Heap)"]
        W1["Worker Thread 1"]
        W2["Worker Thread 2"]
    end

    subgraph DB [("PostgreSQL (oro_base_db)")]
        LOCK["stm_job_locks"]
        EXEC_LOG["stm_job_executions"]
        RETRY["stm_retry_queue"]
    end

    subgraph IPC ["Inter-Process Communication"]
        ZMQ["ZMQ command_executor (PUSH)"]
    end

    TICK -->|1. Scans due jobs| PQ
    PQ -->|2. Dequeue by Priority| W1
    PQ -->|2. Dequeue by Priority| W2
    W1 -->|3. Acquire lock| LOCK
    W1 -->|4. Dispatch Command| ZMQ
    W1 -->|5. Log execution status| EXEC_LOG
    W1 -->|6. Enqueue failure| RETRY
```

### Concurrency Flow
1. **The Scheduler Engine** starts a main **Tick Loop** (running every 1000ms) alongside a configurable **Worker Pool** (defaults to 2 threads).
2. On every tick, the engine scans the active states of all 11 jobs. If a job is enabled and its `next_run_at` timestamp is in the past, it gets pushed into the **Priority Work Queue** (a min-heap organized by urgency: `CRITICAL` > `HIGH` > `MEDIUM` > `LOW`).
3. Workers block on a condition variable until a job is queued. The highest priority job is popped and assigned.
4. **Mutual Exclusion Lock**: Before a worker runs the job's execution handler, it attempts to acquire a database-backed advisory lock for the job name.
   - If the lock is held by another worker or process instance and has not expired, the execution is skipped for this tick.
   - If acquired, the lock is held with a timeout (TTL) equal to `timeout_seconds + 10s`.
5. **Execution & Logging**: The job handler executes. Upon completion:
   - On success: An entry is added to `stm_job_executions` with `status = 'completed'`, and the lock is released.
   - On failure: The error is logged to `stm_job_executions` with `status = 'failed'`, the task is enqueued to `stm_retry_queue` with an calculated exponential delay (30s, 2m, 10m), and the lock is released.

---

## 4. How to Run & What to Expect

### Prerequisites
1. **`storage_handoff` Library**: Must be compiled and installed system-wide:
   ```bash
   cd /home/radxa/oro_base/oro_base_edge_layer/storage_handoff/build
   make && sudo make install
   ```
2. **PostgreSQL Database**: Must be running locally with the target database schema initialized.

### First-time DB Schema Initialization
Execute the SQL script to provision the scheduler-specific tables:
```bash
PGPASSWORD=ogmen psql -h localhost -U oro_user -d oro_base_db -f /home/radxa/oro_base/oro_base_edge_layer/scheduled_task_manager/database/scheduler_schema.sql
```

### Building the Node
```bash
cd /home/radxa/oro_base/oro_base_edge_layer/scheduled_task_manager
mkdir -p build && cd build
cmake ..
make -j$(nproc)
```

### Running Locally (Manual Mode)
Run the compiled executable directly. It will look for the default config file under `/home/radxa/oro_base/oro_base_edge_layer/config/oro_base_edge_layer_config.json`:
```bash
./scheduled_task_manager_node
```

### Running under systemd
The service runs continuously in the background on the Radxa board. Enable and start it using `systemctl`:
```bash
# Copy unit file to systemd configuration
sudo cp /home/radxa/oro_base/oro_base_edge_layer/scheduled_task_manager/scheduled_task_manager.service /etc/systemd/system/
sudo systemctl daemon-reload

# Start and enable on boot
sudo systemctl enable --now scheduled_task_manager.service

# View current service status
sudo systemctl status scheduled_task_manager.service
```

### What to Expect When Running
When running, you should expect logs confirming database connections, job locks being acquired/released, and sensor evaluations printed to standard output or journalctl logs:

```
[STM] Scheduled Task Manager — Starting...
✅ Connected to PostgreSQL
[STM] Database writer initialized.
[JobExecutor] Connected to command_executor at ipc:///tmp/oro_cmd_exec.ipc (PUSH)
[STM] All job SQL statements prepared.
[JobRegistry] Registered 11 jobs.
[SchedulerEngine] Engine started with 11 registered jobs.
[SchedulerEngine] Tick loop started (interval=1000ms).

[JobLock] Acquired lock for 'device_health_check' (TTL: 30s)
[DeviceJobs] device_health_check executing...
[DeviceJobs] device_health_check done. 0 issue(s).
[JobExecutor] Job 'device_health_check' completed successfully in 18ms (items=0)

[JobLock] Acquired lock for 'sensor_data_freshness_check' (TTL: 25s)
[DeviceJobs] Stale sensor: Food Bowl Weight (9999 min, max 30)
[DeviceJobs] Stale sensor: Water Bowl Level (9999 min, max 30)
[DeviceJobs] sensor_data_freshness_check done. 5 stale sensor(s).
[JobExecutor] Job 'sensor_data_freshness_check' completed successfully in 111ms (items=5)

[JobLock] Acquired lock for 'overdue_task_checker' (TTL: 25s)
[CareJobs] 1 overdue feeding schedule(s) found.
[CareJobs] 1 overdue care schedule(s) found.
[CareJobs] overdue_task_checker done. 2 overdue.
[JobExecutor] Job 'overdue_task_checker' completed successfully in 44ms (items=2)
```

---

## 5. Future Scope & Pending Implementations

While the core scheduling, data checking, and logging flows are 100% operational, the following components are scheduled for future development cycles:

### 1. SQLite Local Sync Integration
- **Context**: The edge layer tracks local cloud sync queues in a SQLite database (`sync_queue.db`).
- **TODO**: In `failed_sync_retry` (under `retry_jobs.cpp`), replace the current stdout logging stub with a direct connection to SQLite. The job should read failed synchronization attempts and retry sending them to the cloud bridge.

### 2. Notification Dispatch Infrastructure
- **Context**: The `overdue_task_checker` successfully inserts alerts to the `oro_base_events` table (flagged with `notification_eligible = true`), but the active channel to push this to the user's mobile app is pending.
- **TODO**: Wire up `notification_retry` to integrate with the mobile push gateway API (e.g. Firebase Cloud Messaging or local WebSocket broker) to deliver alerts directly to user devices.

### 3. Database-Driven Configuration
- **Context**: Job frequencies and active switches currently live in `oro_base_edge_layer_config.json`.
- **TODO**: Refactor `SchedulerConfig` to seed from the JSON config on initial boot but query a `scheduled_jobs` table in PostgreSQL dynamically thereafter. This will allow the cloud bridge to modify job intervals at runtime without needing a service restart.

### 4. Media Asset Cleanup
- **Context**: Stale references to image/video assets in the database are cleared, but orphans on disk are not deleted.
- **TODO**: In `data_cleanup` (under `cleanup_jobs.cpp`), implement directory scans for `/home/radxa/Pictures/Command_Executor_Images/` and `/home/radxa/Videos/Command_Executor_Videos/`. The job should cross-reference filenames with the `context_media_assets` table and purge files older than the configured retention threshold.
