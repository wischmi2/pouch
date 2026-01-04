/* pH 2 Click (Mikroe) sensor interface */

#ifndef PH_SENSOR_H_
#define PH_SENSOR_H_

#include <stdint.h>

int ph_sensor_init(void);
void ph_sensor_pouch_session_start(void);
void ph_sensor_pouch_session_end(void);

/* Two-point calibration helpers.
 * Call each while the probe is in a known pH buffer solution.
 */
int ph_sensor_calibrate_low(float known_ph);
int ph_sensor_calibrate_high(float known_ph);

/* Convenience: perform a guided two-point calibration using known
 * low/high pH values (e.g. 7.0 and 4.0) and return 0 on success.
 */
int ph_sensor_guided_calibration(float low_ph, float high_ph);

#endif /* PH_SENSOR_H_ */
