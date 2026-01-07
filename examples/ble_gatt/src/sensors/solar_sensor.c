/* Solar Energy Click battery status reporting for XIAO + ble_gatt node */

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(main);

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>

#include <pouch/uplink.h>
#include <pouch/types.h>

#include "solar_sensor.h"

/* Period between battery status reports (seconds). */
#define SOLAR_REPORT_PERIOD_S 15

/* Solar Energy Click INT wired to XIAO D3 -> nRF P0.29 on GPIO0. */
static const uint8_t solar_pin = 29;
static const struct device *solar_port;

static bool pouch_session_active;
static bool last_batt_ok;

static void solar_report_work_handler(struct k_work *work);

K_WORK_DELAYABLE_DEFINE(solar_report_work, solar_report_work_handler);

static void solar_report_work_handler(struct k_work *work)
{
    ARG_UNUSED(work);

    if (!solar_port || !device_is_ready(solar_port)) {
        LOG_WRN("Solar GPIO port not ready");
        k_work_schedule(&solar_report_work, K_SECONDS(SOLAR_REPORT_PERIOD_S));
        return;
    }

    int level = gpio_pin_get(solar_port, solar_pin);
    if (level < 0) {
        LOG_WRN("Solar INT read failed (err %d)", level);
        k_work_schedule(&solar_report_work, K_SECONDS(SOLAR_REPORT_PERIOD_S));
        return;
    }

    bool batt_ok = (level > 0);

    if (batt_ok != last_batt_ok) {
        LOG_INF("Solar battery status changed: %s", batt_ok ? "OK" : "LOW");
        last_batt_ok = batt_ok;
    }

    /* Periodic status log so you can see it on the console. */
    LOG_INF("Solar battery status: %s", batt_ok ? "OK" : "LOW");

    if (pouch_session_active) {
        char payload[48];
        int len = snprintk(payload, sizeof(payload),
                           "{\"battery_ok\":%s}",
                           batt_ok ? "true" : "false");
        if (len > 0) {
            int err = pouch_uplink_entry_write(".s/solar",
                                               POUCH_CONTENT_TYPE_JSON,
                                               payload,
                                               (size_t)len,
                                               K_NO_WAIT);
            if (err) {
                LOG_WRN("Solar battery uplink failed (err %d)", err);
            }
        }
    }

    k_work_schedule(&solar_report_work, K_SECONDS(SOLAR_REPORT_PERIOD_S));
}

int solar_sensor_init(void)
{
    int err;

    solar_port = DEVICE_DT_GET(DT_NODELABEL(gpio0));
    if (!device_is_ready(solar_port)) {
        LOG_ERR("Solar GPIO port not ready");
        return -ENODEV;
    }

    err = gpio_pin_configure(solar_port, solar_pin, GPIO_INPUT);
    if (err) {
        LOG_ERR("Could not configure Solar GPIO (err %d)", err);
        return err;
    }

    int level = gpio_pin_get(solar_port, solar_pin);
    if (level < 0) {
        LOG_WRN("Solar INT initial read failed (err %d)", level);
        last_batt_ok = false;
    } else {
        last_batt_ok = (level > 0);
    }

    LOG_INF("Solar battery initial status: %s", last_batt_ok ? "OK" : "LOW");

    k_work_schedule(&solar_report_work, K_SECONDS(SOLAR_REPORT_PERIOD_S));

    return 0;
}

void solar_sensor_pouch_session_start(void)
{
    pouch_session_active = true;
}

void solar_sensor_pouch_session_end(void)
{
    pouch_session_active = false;
}
