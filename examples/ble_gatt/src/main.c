/*
 * Copyright (c) 2025 Golioth
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main);

#include "credentials.h"

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include <zephyr/shell/shell.h>

#include "sensors/sensor.h"
#include "sensors/ph_sensor.h"

#include <pouch/pouch.h>
#include <pouch/events.h>
#include <pouch/uplink.h>
#include <pouch/downlink.h>
#include <pouch/transport/gatt/peripheral.h>
#include <pouch/transport/gatt/common/types.h>

#include <golioth/golioth.h>
#include <golioth/settings_callbacks.h>

#include <app_version.h>

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET_OR(DT_ALIAS(led0), gpios, {});
static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET_OR(DT_ALIAS(sw0), gpios, {});
static struct gpio_callback button_cb_data;
static struct bt_conn *default_conn;

static void button_pressed(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
    if (default_conn)
    {
        LOG_INF("Confirming passkey");
        bt_conn_auth_passkey_confirm(default_conn);
    }
    else
    {
        LOG_WRN("No BT connection for passkey confirmation");
    }
}

static struct
{
    uint16_t uuid;
    struct pouch_gatt_adv_data data;
} __packed service_data = {
    .uuid = POUCH_GATT_UUID_SVC_VAL_16,
    .data =
        {
            .version = (POUCH_VERSION << POUCH_GATT_ADV_VERSION_POUCH_SHIFT)
                | (POUCH_GATT_VERSION << POUCH_GATT_ADV_VERSION_SELF_SHIFT),
            .flags = 0x0,
        },
};

static struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA(BT_DATA_SVC_DATA16, &service_data, sizeof(service_data)),
    BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

static void connected(struct bt_conn *conn, uint8_t err)
{
    if (err)
    {
        LOG_INF("Connection failed (err 0x%02x)", err);
    }
    else
    {
        LOG_INF("Connected");
        default_conn = conn;
    }
}

void disconnect_work_handler(struct k_work *work)
{
    int err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_2, ad, ARRAY_SIZE(ad), NULL, 0);
    if (err)
    {
        LOG_ERR("Advertising failed to start (err %d)", err);
    }
}

K_WORK_DELAYABLE_DEFINE(disconnect_work, disconnect_work_handler);

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
    LOG_INF("Disconnected (reason 0x%02x)", reason);

    default_conn = NULL;

    k_work_schedule(&disconnect_work, K_SECONDS(1));
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
    .connected = connected,
    .disconnected = disconnected,
};

static void auth_passkey_display(struct bt_conn *conn, unsigned int passkey)
{
    char addr[BT_ADDR_LE_STR_LEN];
    char passkey_str[7];

    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

    (void) snprintf(passkey_str, sizeof(passkey_str), "%06u", passkey);

    LOG_INF("Passkey for %s: %s", addr, passkey_str);
}

static void auth_passkey_confirm(struct bt_conn *conn, unsigned int passkey)
{
    char addr[BT_ADDR_LE_STR_LEN];
    char passkey_str[7];

    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

    (void) snprintf(passkey_str, sizeof(passkey_str), "%06u", passkey);

    LOG_INF("Confirm passkey for %s: %s", addr, passkey_str);

    if (IS_ENABLED(CONFIG_EXAMPLE_BT_AUTO_CONFIRM))
    {
        LOG_INF("Confirming passkey");
        bt_conn_auth_passkey_confirm(conn);
    }
}

static void auth_cancel(struct bt_conn *conn)
{
    char addr[BT_ADDR_LE_STR_LEN];

    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

    LOG_INF("Pairing cancelled: %s", addr);
}

static struct bt_conn_auth_cb auth_cb_display = {
    .passkey_display = auth_passkey_display,
    .passkey_confirm = auth_passkey_confirm,
    .cancel = auth_cancel,
};

void sync_request_work_handler(struct k_work *work)
{
    service_data.data.flags |= POUCH_GATT_ADV_FLAG_SYNC_REQUEST;
    bt_le_adv_update_data(ad, ARRAY_SIZE(ad), NULL, 0);
    LOG_INF("Sync request flag set in advertisement");
}

K_WORK_DELAYABLE_DEFINE(sync_request_work, sync_request_work_handler);

static void pouch_event_handler(enum pouch_event event, void *ctx)
{
    LOG_INF("Pouch event: %d", event);

    if (POUCH_EVENT_SESSION_START == event)
    {
        pouch_uplink_entry_write(".s/sensor",
                                 POUCH_CONTENT_TYPE_JSON,
                                 "{\"temp\":22}",
                                 sizeof("{\"temp\":22}") - 1,
                                 K_FOREVER);

        sensors_pouch_session_start();

        golioth_sync_to_cloud();
    }

    if (POUCH_EVENT_SESSION_END == event)
    {
        sensors_pouch_session_end();

        service_data.data.flags &= ~POUCH_GATT_ADV_FLAG_SYNC_REQUEST;
        bt_le_adv_update_data(ad, ARRAY_SIZE(ad), NULL, 0);
        k_work_schedule(&sync_request_work, K_SECONDS(CONFIG_EXAMPLE_SYNC_PERIOD_S));
    }
}

POUCH_EVENT_HANDLER(pouch_event_handler, NULL);

static int led_setting_cb(bool new_value)
{
    LOG_INF("Received LED setting: %d", (int) new_value);

    if (DT_HAS_ALIAS(led0))
    {
        gpio_pin_set_dt(&led, new_value ? 1 : 0);
    }

    return 0;
}

GOLIOTH_SETTINGS_HANDLER(LED, led_setting_cb);

/*
 * pH calibration shell commands
 *
 * Usage:
 *   ph calib-low <ph>
 *   ph calib-high <ph>
 *   ph calib-guided [low_ph] [high_ph]
 */

static int cmd_ph_calib_low(const struct shell *sh, size_t argc, char **argv)
{
    if (argc < 2) {
        shell_error(sh, "Usage: ph calib-low <ph>");
        return -EINVAL;
    }

    float ph = strtof(argv[1], NULL);
    int err = ph_sensor_calibrate_low(ph);
    if (err) {
        shell_error(sh, "Calibration failed (err %d)", err);
    } else {
        shell_print(sh, "Captured low-point calibration at pH=%.3f", (double)ph);
    }

    return err;
}

static int cmd_ph_calib_high(const struct shell *sh, size_t argc, char **argv)
{
    if (argc < 2) {
        shell_error(sh, "Usage: ph calib-high <ph>");
        return -EINVAL;
    }

    float ph = strtof(argv[1], NULL);
    int err = ph_sensor_calibrate_high(ph);
    if (err) {
        shell_error(sh, "Calibration failed (err %d)", err);
    } else {
        shell_print(sh, "Captured high-point calibration at pH=%.3f", (double)ph);
    }

    return err;
}

static int cmd_ph_calib_guided(const struct shell *sh, size_t argc, char **argv)
{
    float low_ph = 7.0f;
    float high_ph = 4.0f;

    if (argc >= 2) {
        low_ph = strtof(argv[1], NULL);
    }
    if (argc >= 3) {
        high_ph = strtof(argv[2], NULL);
    }

    shell_print(sh, "Guided calibration (non-interactive): low=%.2f high=%.2f",
                (double)low_ph, (double)high_ph);
    shell_print(sh, "Make sure the probe is in the LOW buffer (%.2f) before running,",
                (double)low_ph);
    shell_print(sh, "then move it to the HIGH buffer (%.2f) when instructed.",
                (double)high_ph);

    int err = ph_sensor_calibrate_low(low_ph);
    if (err) {
        shell_error(sh, "Low-point calibration failed (err %d)", err);
        return err;
    }

    shell_print(sh, "Low-point captured. Now move the probe to the HIGH buffer and run the command again if needed, or use ph calib-high.");

    /* For safety, do not automatically capture the high point here.
     * Users can explicitly call ph calib-high <ph> after moving the probe.
     */
    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(ph_sub,
    SHELL_CMD(calib-low, NULL, "Capture low-point calibration: ph calib-low <ph>", cmd_ph_calib_low),
    SHELL_CMD(calib-high, NULL, "Capture high-point calibration: ph calib-high <ph>", cmd_ph_calib_high),
    SHELL_CMD(calib-guided, NULL, "Run guided two-point calibration: ph calib-guided [low_ph] [high_ph]", cmd_ph_calib_guided),
    SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(ph, &ph_sub, "pH sensor commands", NULL);

int main(void)
{
    printk("ble_gatt node booting\r\n");
    LOG_INF("Pouch SDK Version: " STRINGIFY(APP_BUILD_VERSION));
    LOG_INF("Pouch Protocol Version: %d", POUCH_VERSION);
    LOG_INF("Pouch BLE Transport Protocol Version: %d", POUCH_GATT_VERSION);

    /* Inform the user how to perform calibration from the serial console. */
    printk("\r\nTo calibrate the pH sensor, use shell commands: \r\n");
    printk("  ph calib-low <ph>   (e.g. ph calib-low 7.00)\r\n");
    printk("  ph calib-high <ph>  (e.g. ph calib-high 4.00)\r\n");
    printk("or start with: ph calib-guided 7.00 4.00\r\n");
    printk("If you do not wish to calibrate now, ignore this message and the node will continue.\r\n");

    int err = pouch_gatt_peripheral_init();
    if (err)
    {
        LOG_ERR("Failed to initialize Pouch BLE GATT peripheral (err %d)", err);
        return 0;
    }

    err = bt_enable(NULL);
    if (err)
    {
        LOG_ERR("Bluetooth init failed (err %d)", err);
        return 0;
    }

    err = bt_conn_auth_cb_register(&auth_cb_display);
    if (err)
    {
        LOG_ERR("Bluetooth auth cb register failed (err %d)", err);
        return err;
    }

    LOG_INF("Bluetooth initialized");

    struct pouch_config config = {0};

    err = load_certificate(&config.certificate);
    if (err)
    {
        LOG_ERR("Failed to load certificate (err %d)", err);
        return 0;
    }

    config.private_key = load_private_key();
    if (config.private_key == PSA_KEY_ID_NULL)
    {
        LOG_ERR("Failed to load private key");
        return 0;
    }

    LOG_INF("Credentials loaded");

    err = pouch_init(&config);
    if (err)
    {
        LOG_ERR("Pouch init failed (err %d)", err);
        return 0;
    }

    LOG_INF("Pouch initialized");

    err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_2, ad, ARRAY_SIZE(ad), NULL, 0);
    if (err)
    {
        LOG_ERR("Advertising failed to start (err %d)", err);
        return 0;
    }

    LOG_INF("Advertising started");

    if (DT_HAS_ALIAS(led0))
    {
        err = gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
        if (err < 0)
        {
            LOG_ERR("Could not initialize LED");
        }
    }

    if (DT_HAS_ALIAS(sw0))
    {
        LOG_INF("Set up button at %s pin %d", button.port->name, button.pin);

        err = gpio_pin_configure_dt(&button, GPIO_INPUT);
        if (err < 0)
        {
            LOG_ERR("Could not initialize Button");
        }

        err = gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_TO_ACTIVE);
        if (err)
        {
            LOG_ERR("Error %d: failed to configure interrupt on %s pin %d",
                    err,
                    button.port->name,
                    button.pin);
            return 0;
        }

        gpio_init_callback(&button_cb_data, button_pressed, BIT(button.pin));
        gpio_add_callback(button.port, &button_cb_data);
    }

    err = sensors_init_all();
    if (err) {
        LOG_ERR("Sensors init failed (err %d)", err);
    }

    k_work_schedule(&sync_request_work, K_SECONDS(CONFIG_EXAMPLE_SYNC_PERIOD_S));

    while (1)
    {
        k_sleep(K_SECONDS(1));
    }
    return 0;
}
