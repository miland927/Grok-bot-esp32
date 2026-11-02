#pragma once

#include <stdbool.h>

#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

/* PCF85063 RTC on the shared board I2C bus (address 0x51). */
bool pet_rtc_start(i2c_master_bus_handle_t bus);
bool pet_rtc_read(int* hour, int* minute, int* weekday);
bool pet_rtc_set(int year, int mon, int day, int wday, int hour, int min, int sec);

#ifdef __cplusplus
}
#endif


