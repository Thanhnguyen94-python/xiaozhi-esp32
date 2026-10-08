#ifndef BREAD_COMPACT_WIFI_RC_SBUS_CONTROLLER_H
#define BREAD_COMPACT_WIFI_RC_SBUS_CONTROLLER_H

#include "dual_dc_motor_controller.h"

#include <stdint.h>

namespace rc_sbus {

struct State {
    bool initialized = false;
    bool link_ok = false;
    bool frame_lost = false;
    bool failsafe = true;
    bool override_active = false;
    int16_t channels_raw[16] = {0};
    int16_t throttle_255 = 0;
    int16_t steering_255 = 0;
    int16_t left_255 = 0;
    int16_t right_255 = 0;
    uint32_t last_frame_ms = 0;
};

// Initialize SBUS receiver and RC override task.
// Safe to call multiple times.
void Init(DualDcMotorController* wheel_motor);

// true when RC stick input is outside deadzone and RC link is valid.
bool IsOverrideActive();

// Snapshot current RC runtime state.
bool GetState(State& out);

}  // namespace rc_sbus

#endif  // BREAD_COMPACT_WIFI_RC_SBUS_CONTROLLER_H
