/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  Copyright (C) 2014, Samsung Electronics Co. Ltd. All Rights Reserved.
 */
#ifndef _SSP_SENSORS_H_
#define _SSP_SENSORS_H_

#include <linux/iio/iio.h>

#define SSP_TIME_SIZE				4
#define SSP_ACCELEROMETER_SIZE			6
#define SSP_GYROSCOPE_SIZE			6
#define SSP_BIO_HRM_RAW_SIZE			8
#define SSP_BIO_HRM_RAW_FAC_SIZE		36
#define SSP_BIO_HRM_LIB_SIZE			8

/**
 * enum ssp_sensor_type - SSP sensor type
 */
enum ssp_sensor_type {
	SSP_ACCELEROMETER_SENSOR = 0,
	SSP_GYROSCOPE_SENSOR,
	SSP_GEOMAGNETIC_UNCALIB_SENSOR,
	SSP_GEOMAGNETIC_RAW,
	SSP_GEOMAGNETIC_SENSOR,
	SSP_PRESSURE_SENSOR,
	SSP_GESTURE_SENSOR,
	SSP_PROXIMITY_SENSOR,
	SSP_TEMPERATURE_HUMIDITY_SENSOR,
	SSP_LIGHT_SENSOR,
	SSP_PROXIMITY_RAW,
	SSP_ORIENTATION_SENSOR,
	SSP_STEP_DETECTOR,
	SSP_SIG_MOTION_SENSOR,
	SSP_GYRO_UNCALIB_SENSOR,
	SSP_GAME_ROTATION_VECTOR,
	SSP_ROTATION_VECTOR,
	SSP_STEP_COUNTER,
	SSP_BIO_HRM_RAW,
	SSP_BIO_HRM_RAW_FAC,
	SSP_BIO_HRM_LIB,
	SSP_SENSOR_MAX,
};

struct ssp_data;

/**
 * struct ssp_sensor_data - Sensor object
 * @process_data:	Callback to feed sensor data.
 * @type:		Used sensor type.
 * @buffer:		Received data buffer.
 */
struct ssp_sensor_data {
	int (*process_data)(struct iio_dev *indio_dev, void *buf,
			    int64_t timestamp);
	enum ssp_sensor_type type;
	u8 *buffer;
};

void ssp_register_consumer(struct iio_dev *indio_dev,
			   enum ssp_sensor_type type);

int ssp_enable_sensor(struct ssp_data *data, enum ssp_sensor_type type,
		      u32 delay);

int ssp_disable_sensor(struct ssp_data *data, enum ssp_sensor_type type);

u32 ssp_get_sensor_delay(struct ssp_data *data, enum ssp_sensor_type);

int ssp_change_delay(struct ssp_data *data, enum ssp_sensor_type type,
		     u32 delay);
#endif /* _SSP_SENSORS_H_ */
