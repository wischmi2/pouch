/* DS18B20 1-Wire temperature sensor handling for XIAO + pH 2 Click */

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(main);

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>

#include <pouch/uplink.h>
#include <pouch/types.h>

#include "temp_sensor.h"

/* Period between temperature reports (seconds). */
#define TEMP_REPORT_PERIOD_S 15

/* Get any enabled DS18B20 instance from devicetree. */
static const struct device *temp_dev;
static bool pouch_session_active;

static void temp_report_work_handler(struct k_work *work);

K_WORK_DELAYABLE_DEFINE(temp_report_work, temp_report_work_handler);

static void temp_report_work_handler(struct k_work *work)
{
    ARG_UNUSED(work);

    if (!temp_dev || !device_is_ready(temp_dev)) {
        k_work_schedule(&temp_report_work, K_SECONDS(TEMP_REPORT_PERIOD_S));
        return;
    }

    struct sensor_value temp;
    int err = sensor_sample_fetch(temp_dev);
    if (err) {
        LOG_WRN("Temp sensor sample_fetch failed (err %d)", err);
        k_work_schedule(&temp_report_work, K_SECONDS(TEMP_REPORT_PERIOD_S));
        return;
    }

    err = sensor_channel_get(temp_dev, SENSOR_CHAN_AMBIENT_TEMP, &temp);
    if (err) {
        LOG_WRN("Temp sensor channel_get failed (err %d)", err);
        k_work_schedule(&temp_report_work, K_SECONDS(TEMP_REPORT_PERIOD_S));
        return;
    }

    /* temp.val1 = integer Celsius, temp.val2 = fractional in 1e-6 degC */
    int32_t temp_milli = temp.val1 * 1000 + temp.val2 / 1000;
    int32_t t_int = temp_milli / 1000;
    int32_t t_frac = temp_milli >= 0 ? (temp_milli % 1000) : -(temp_milli % 1000);

    LOG_INF("Temp reading: %d.%03d C", (int)t_int, (int)t_frac);

    if (pouch_session_active) {
        char payload[64];
        int len = snprintk(payload, sizeof(payload),
                           "{\"temp_c\":%d.%03d}",
                           (int)t_int,
                           (int)t_frac);
        if (len > 0) {
            err = pouch_uplink_entry_write(".s/temp",
                                           POUCH_CONTENT_TYPE_JSON,
                                           payload,
                                           (size_t)len,
                                           K_NO_WAIT);
            if (err) {
                LOG_WRN("Temp uplink failed (err %d), logging only", err);
            }
        }
    }

    k_work_schedule(&temp_report_work, K_SECONDS(TEMP_REPORT_PERIOD_S));
}

int temp_sensor_init(void)
{
    temp_dev = DEVICE_DT_GET_ANY(maxim_ds18b20);
    if (!temp_dev) {
        LOG_WRN("No DS18B20 device found in devicetree; temp sensor disabled");
        return -ENODEV;
    }

    if (!device_is_ready(temp_dev)) {
        LOG_WRN("DS18B20 device %s not ready", temp_dev->name);
        return -EIO;
    }

    LOG_INF("DS18B20 temp sensor initialized: %s", temp_dev->name);

    k_work_schedule(&temp_report_work, K_SECONDS(TEMP_REPORT_PERIOD_S));

    return 0;
}

void temp_sensor_pouch_session_start(void)
{
    pouch_session_active = true;
}

void temp_sensor_pouch_session_end(void)
{
    pouch_session_active = false;
}
