#pragma once

#include "device_state.h"

constexpr bool CanRunPetShow(DeviceState state) {
    return state == kDeviceStateIdle || state == kDeviceStateWifiConfiguring;
}
