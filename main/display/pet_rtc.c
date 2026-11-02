#include "pet_rtc.h"

#include <esp_log.h>

static const char* TAG = "PetRtc";

#define PCF85063_ADDR 0x51
#define REG_SECONDS 0x04

static i2c_master_dev_handle_t s_dev;

static int bcd(uint8_t v) {
    return (v >> 4) * 10 + (v & 0x0F);
}

bool pet_rtc_start(i2c_master_bus_handle_t bus) {
    if (s_dev) {
        return true;
    }
    if (!bus) {
        return false;
    }

    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = PCF85063_ADDR,
        .scl_speed_hz = 100000,
    };
    if (i2c_master_bus_add_device(bus, &cfg, &s_dev) != ESP_OK) {
        ESP_LOGW(TAG, "add device failed");
        return false;
    }
    return true;
}

bool pet_rtc_read(int* hour, int* minute, int* weekday) {
    if (!s_dev) {
        return false;
    }

    uint8_t reg = REG_SECONDS;
    uint8_t raw[6];
    if (i2c_master_transmit_receive(s_dev, &reg, 1, raw, sizeof(raw), 100) != ESP_OK) {
        return false;
    }

    int sec = bcd(raw[0] & 0x7F);
    int min = bcd(raw[1] & 0x7F);
    int hr = bcd(raw[2] & 0x3F);
    if (hr > 23 || min > 59 || sec > 59) {
        return false;
    }
    /* an all-zero clock means the RTC has never been set */
    if (hr == 0 && min == 0 && sec == 0 && (raw[0] & 0x7F) == 0) {
        return false;
    }

    *hour = hr;
    *minute = min;
    *weekday = raw[4] & 0x07;
    return true;
}

static uint8_t to_bcd(int v) {
    return (uint8_t)(((v / 10) << 4) | (v % 10));
}

bool pet_rtc_set(int year, int mon, int day, int wday, int hour, int min, int sec) {
    if (!s_dev) {
        return false;
    }
    if (year < 2000 || year > 2099 || mon < 1 || mon > 12 || day < 1 || day > 31 || hour < 0 ||
        hour > 23 || min < 0 || min > 59 || sec < 0 || sec > 59) {
        return false;
    }

    uint8_t buf[8];
    buf[0] = REG_SECONDS;
    buf[1] = to_bcd(sec);
    buf[2] = to_bcd(min);
    buf[3] = to_bcd(hour);
    buf[4] = to_bcd(day);
    buf[5] = (uint8_t)(wday & 0x07);
    buf[6] = to_bcd(mon);
    buf[7] = to_bcd(year - 2000);

    return i2c_master_transmit(s_dev, buf, sizeof(buf), 100) == ESP_OK;
}

