#ifndef COMMAND_EXECUTOR_COMMAND_REGISTRY_HPP
#define COMMAND_EXECUTOR_COMMAND_REGISTRY_HPP

#include "command_executor/command.hpp"
#include <array>

namespace oro {

struct SignalDescriptor {
    uint16_t        signal_id;
    const char*     signal_type;
    SignalDirection direction;
    ValueType       value_type;
    uint32_t        timeout_ms;
    const char*     display_name;
};

static constexpr std::array<SignalDescriptor, 20> COMMAND_REGISTRY = {{
    { 84,  "manual_lid_open_command_event",   SignalDirection::INBOUND,  ValueType::EVENT,   3000,  "Manual Lid Open Command Event"    },
    { 85,  "treat_dispense_command_event",    SignalDirection::INBOUND,  ValueType::EVENT,   20000, "Treat Dispense Command Event"     },
    { 123, "manual_lid_close_command_event",  SignalDirection::INBOUND,  ValueType::EVENT,   3000,  "Manual Lid Close Command Event"   },
    { 64,  "lid_actuation_command",           SignalDirection::INBOUND,  ValueType::ENUM,    3000,  "Lid Actuation Command"            },
    { 65,  "lid_actuation_result",            SignalDirection::OUTBOUND, ValueType::ENUM,    0,     "Lid Actuation Result"             },
    { 125, "treat_dispensed_quantity",         SignalDirection::OUTBOUND, ValueType::NUMERIC, 0,     "Treat Dispensed Quantity"         },
    { 126, "treat_dispense_confirmation",     SignalDirection::OUTBOUND, ValueType::BOOLEAN, 0,     "Treat Dispense Confirmation"      },
    { 91,  "photo_capture_command_event",     SignalDirection::INBOUND,  ValueType::EVENT,   5000,  "Photo Capture Command Event"      },
    { 93,  "image_file_save_confirmation",    SignalDirection::OUTBOUND, ValueType::BOOLEAN, 0,     "Image File Save Confirmation"     },
    { 88,  "live_session_start_event",        SignalDirection::INBOUND,  ValueType::EVENT,   10000, "Live Session Start Event"         },
    { 133, "live_session_end_event",          SignalDirection::INBOUND,  ValueType::EVENT,   10000, "Live Session End Event"           },
    { 98,  "settings_apply_success_status",   SignalDirection::INBOUND,  ValueType::BOOLEAN, 5000,  "Settings Apply Success Status"    },
    { 134, "camera_rotation_command",         SignalDirection::INBOUND,  ValueType::NUMERIC, 5000,  "Camera Rotation Command"          },
    { 135, "video_capture_command_event",    SignalDirection::INBOUND,  ValueType::EVENT,   15000, "Video Capture Command Event"       },
    { 136, "video_file_save_confirmation",   SignalDirection::OUTBOUND, ValueType::BOOLEAN, 0,     "Video File Save Confirmation"      },
}};

} // namespace oro

#endif // COMMAND_EXECUTOR_COMMAND_REGISTRY_HPP
