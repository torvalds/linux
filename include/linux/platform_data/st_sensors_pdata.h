/*
 * STMicroelectronics sensors platform-data driver
 *
 * Copyright 2013 STMicroelectronics Inc.
 *
 * Denis Ciocca <denis.ciocca@st.com>
 *
 * Licensed under the GPL-2.
 */

#ifndef ST_SENSORS_PDATA_H
#define ST_SENSORS_PDATA_H

/**
 * struct st_sensors_platform_data - Platform data for the ST sensors
 * @drdy_int_pin: Redirect DRDY on pin 1 (1) or pin 2 (2).
 *	Available only for accelerometer and pressure sensors.
 *	Accelerometer DRDY on LSM330 available only on pin 1 (see datasheet).
 */
struct st_sensors_platform_data {
	u8 drdy_int_pin;
};

#endif /* ST_SENSORS_PDATA_H */
