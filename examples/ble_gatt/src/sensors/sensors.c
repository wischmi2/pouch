/* Aggregates all sensors used by the ble_gatt node. */

#include "sensor.h"
#include "water_sensor.h"
#include "ph_sensor.h"
#include "solar_sensor.h"

int sensors_init_all(void)
{
    int err;

    err = water_sensor_init();
    if (err) {
        return err;
    }

    err = ph_sensor_init();
    if (err) {
        return err;
    }

    /* Solar Energy Click is optional; if not present, continue. */
    (void)solar_sensor_init();

    return 0;
}

void sensors_pouch_session_start(void)
{
    water_sensor_pouch_session_start();
    ph_sensor_pouch_session_start();
    solar_sensor_pouch_session_start();
}

void sensors_pouch_session_end(void)
{
    water_sensor_pouch_session_end();
    ph_sensor_pouch_session_end();
     solar_sensor_pouch_session_end();
}
