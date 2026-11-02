#pragma once

#include <stdbool.h>

#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

bool pet_imu_start(i2c_master_bus_handle_t bus);
bool pet_imu_read(float* tilt_x, float* tilt_y, float* gyro_dps, float* acc_dev_g);

#ifdef __cplusplus
}
#endif
