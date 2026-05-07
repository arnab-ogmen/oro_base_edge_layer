#ifndef HEALTH_MONITORING_CONFIG_HPP
#define HEALTH_MONITORING_CONFIG_HPP
// ============================================================================
// config.hpp — Health Monitoring Node Configuration
//
// Centralizes all tuneable parameters: device identity, ZMQ endpoints,
// emission intervals, and threshold constants.
//
// Device ID is hardcoded for the current prototype device. A placeholder
// function exists for future master config file integration.
// ============================================================================

#include <cstdint>
#include <string>

namespace oro::health {

// ── Device Identity ─────────────────────────────────────────────────────────

/// Current device UUID (hardcoded for prototype unit).
/// This maps to oro_base_device.device_id in the signals table FK.
static constexpr const char *DEVICE_ID =
    "9e092b69-5973-46e4-a228-fe4933e04364";

// TODO: Implement load_device_id_from_config() to read from the master config
//       file after user setup. The master config path will be determined by the
//       provisioning flow. Until then, DEVICE_ID above is used directly.
//
// Expected signature:
//   std::string load_device_id_from_config(const std::string& config_path);
//
// Do NOT call this function yet — it is a placeholder for future scope.
inline std::string load_device_id_from_config(const std::string & /*config_path*/) {
  // TODO: Parse master config JSON/YAML and extract device_id field.
  //       Return the UUID string from the config file.
  //       Fallback to DEVICE_ID if config is missing or malformed.
  return DEVICE_ID;
}

// ── ZMQ Endpoint Configuration ──────────────────────────────────────────────

/// IPC endpoints published by oro_base_input_layer
static constexpr const char *SENSOR_IPC_ENDPOINT =
    "ipc:///tmp/oro_sensors.ipc";
static constexpr const char *SYSTEM_IPC_ENDPOINT =
    "ipc:///tmp/oro_system.ipc";
static constexpr const char *STATUS_IPC_ENDPOINT =
    "ipc:///tmp/oro_status.ipc";

// ── Emission Intervals (milliseconds) ───────────────────────────────────────

/// #71 device_heartbeat — periodic every 10 seconds
static constexpr uint64_t HEARTBEAT_INTERVAL_MS = 10000;

/// #73 battery_level — periodic snapshot every 60 seconds
static constexpr uint64_t BATTERY_SNAPSHOT_INTERVAL_MS = 60000;

/// #73 battery_level — significant change threshold (percentage points)
static constexpr double BATTERY_CHANGE_THRESHOLD = 1.0;

/// #76 frame_brightness_contrast_quality — periodic every 30 seconds
static constexpr uint64_t FRAME_QUALITY_INTERVAL_MS = 30000;

/// #106 last_seen_timestamp — periodic emission interval (30 seconds)
static constexpr uint64_t LAST_SEEN_INTERVAL_MS = 30000;

/// Main tick loop sleep interval
static constexpr uint64_t TICK_SLEEP_MS = 100;

// ── Firmware Config File ────────────────────────────────────────────────────

/// Path to the firmware metadata config file on disk.
/// This file stores current firmware versions, latest available versions,
/// and update state per component.
static constexpr const char *FIRMWARE_CONFIG_PATH =
    "/etc/oro/firmware_config.json";

// TODO: Implement firmware config file read/write.
//       The file format should be JSON with structure:
//       {
//         "components": {
//           "mcu": {
//             "current_version": "1.0.0",
//             "latest_version": "1.1.0",
//             "update_available": true,
//             "update_status": "idle",
//             "build_id": "abc123"
//           },
//           "host": { ... }
//         }
//       }

/// Placeholder: Read firmware config from disk.
/// Returns empty string on failure. Caller parses the JSON.
inline std::string read_firmware_config(const std::string & /*path*/) {
  // TODO: Read file at `path` and return its contents as a string.
  //       Return empty string if file does not exist or is unreadable.
  return "";
}

/// Placeholder: Write firmware config to disk.
/// Returns true on success.
inline bool write_firmware_config(const std::string & /*path*/,
                                  const std::string & /*json_content*/) {
  // TODO: Write `json_content` to file at `path`.
  //       Create parent directories if needed.
  //       Return false on I/O error.
  return false;
}

} // namespace oro::health

#endif // HEALTH_MONITORING_CONFIG_HPP
