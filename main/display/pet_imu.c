#include "pet_imu.h"

/* qmi8658.h defines M_PI, so it must come before math.h */
#include "qmi8658.h"

#include <math.h>

#include <esp_log.h>

static const char* TAG = "PetImu";

static qmi8658_dev_t s_imu;
static bool s_ready = false;

bool pet_imu_start(i2c_master_bus_handle_t bus) {
    if (s_ready) {
        return true;
    }
    if (!bus) {
        return false;
    }

    esp_err_t err = qmi8658_init(&s_imu, bus, QMI8658_ADDRESS_HIGH);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "init failed: %s", esp_err_to_name(err));
        return false;
    }

    qmi8658_set_accel_range(&s_imu, QMI8658_ACCEL_RANGE_8G);
    qmi8658_set_accel_odr(&s_imu, QMI8658_ACCEL_ODR_500HZ);
    qmi8658_set_gyro_range(&s_imu, QMI8658_GYRO_RANGE_512DPS);
    qmi8658_set_gyro_odr(&s_imu, QMI8658_GYRO_ODR_500HZ);
    qmi8658_set_accel_unit_mps2(&s_imu, true);
    qmi8658_write_register(&s_imu, QMI8658_CTRL5, 0x03);
    qmi8658_write_register(&s_imu, QMI8658_CTRL7, 0x03);

    s_ready = true;
    ESP_LOGI(TAG, "ready at 0x%02x", QMI8658_ADDRESS_HIGH);
    return true;
}

bool pet_imu_read(float* tilt_x, float* tilt_y, float* gyro_dps, float* acc_dev_g) {
    if (!s_ready) {
        return false;
    }

    qmi8658_data_t d;
    if (qmi8658_read_sensor_data(&s_imu, &d) != ESP_OK) {
        return false;
    }

    const float g0 = 9.80665f;
    float ax = d.accelX / g0;
    float ay = d.accelY / g0;
    float az = d.accelZ / g0;
    float norm = sqrtf(ax * ax + ay * ay + az * az);

    *acc_dev_g = fabsf(norm - 1.0f);
    *gyro_dps = sqrtf(d.gyroX * d.gyroX + d.gyroY * d.gyroY + d.gyroZ * d.gyroZ);
    *tilt_x = ay;
    *tilt_y = -ax;
    return true;
}
