/* Water Detect 3 Click handling for XIAO nRF52840 node */

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(main);

#include <zephyr/drivers/gpio.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>

#include <pouch/uplink.h>
#include <pouch/types.h>

#include "water_sensor.h"

/* Water Detect 3 INT wired to XIAO D2 -> nRF P0.28 on GPIO0 */
static const uint8_t water_pin = 28;
static const struct device *water_port;
static struct gpio_callback water_cb_data;
static bool water_wet;
static int64_t water_last_change_ms;
static bool pouch_session_active;

static void water_report_work_handler(struct k_work *work)
{
    ARG_UNUSED(work);

    if (!pouch_session_active) {
        LOG_WRN("Water state change but Pouch session not active");
        return;
    }

    const char *payload = water_wet ? "{\"wet\":true}" : "{\"wet\":false}";
    size_t len = strlen(payload);

    int err = pouch_uplink_entry_write(".s/water",
                                       POUCH_CONTENT_TYPE_JSON,
                                       payload,
                                       len,
                                       K_NO_WAIT);
    if (err) {
        LOG_ERR("Failed to send water state (err %d)", err);
    } else {
        LOG_INF("Water state reported to Pouch: %s", water_wet ? "WET" : "DRY");
    }
}

K_WORK_DEFINE(water_report_work, water_report_work_handler);

static void water_int_triggered(const struct device *dev,
                                struct gpio_callback *cb,
                                uint32_t pins)
{
    ARG_UNUSED(dev);
    ARG_UNUSED(cb);
    ARG_UNUSED(pins);

    bool level = gpio_pin_get(water_port, water_pin) > 0;
    int64_t now = k_uptime_get();

    /* Simple debounce: only act on real edge changes spaced in time */
    if (level == water_wet) {
        return;
    }

    if ((now - water_last_change_ms) < 200) {
        return;
    }

    water_wet = level;
    water_last_change_ms = now;

    LOG_INF("Water state changed: %s", water_wet ? "WET" : "DRY");

    k_work_submit(&water_report_work);
}

int water_sensor_init(void)
{
    int err;

    water_port = DEVICE_DT_GET(DT_NODELABEL(gpio0));
    if (!device_is_ready(water_port)) {
        LOG_ERR("Water sensor GPIO port not ready");
        return -ENODEV;
    }

    err = gpio_pin_configure(water_port, water_pin, GPIO_INPUT);
    if (err < 0) {
        LOG_ERR("Could not initialize water sensor GPIO (err %d)", err);
        return err;
    }

    err = gpio_pin_interrupt_configure(water_port,
                                       water_pin,
                                       GPIO_INT_EDGE_BOTH);
    if (err) {
        LOG_ERR("Failed to configure water sensor interrupt (err %d)", err);
        return err;
    }

    gpio_init_callback(&water_cb_data,
                       water_int_triggered,
                       BIT(water_pin));
    gpio_add_callback(water_port, &water_cb_data);

    water_wet = gpio_pin_get(water_port, water_pin) > 0;
    water_last_change_ms = k_uptime_get();

    LOG_INF("Water sensor GPIO configured on P0.%d, initial state: %s",
            water_pin,
            water_wet ? "WET" : "DRY");

    return 0;
}

void water_sensor_pouch_session_start(void)
{
    pouch_session_active = true;
    /* Report current state when a session comes up */
    k_work_submit(&water_report_work);
}

void water_sensor_pouch_session_end(void)
{
    pouch_session_active = false;
}
