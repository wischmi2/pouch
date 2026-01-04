/* DS18B20 temperature sensor interface for pH board */

#ifndef TEMP_SENSOR_H_
#define TEMP_SENSOR_H_

int temp_sensor_init(void);
void temp_sensor_pouch_session_start(void);
void temp_sensor_pouch_session_end(void);

#endif /* TEMP_SENSOR_H_ */
