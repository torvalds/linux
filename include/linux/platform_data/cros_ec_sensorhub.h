/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Chrome OS EC MEMS Sensor Hub driver.
 *
 * Copyright 2019 Google LLC
 */

#ifndef __LINUX_PLATFORM_DATA_CROS_EC_SENSORHUB_H
#define __LINUX_PLATFORM_DATA_CROS_EC_SENSORHUB_H

#include <linux/ktime.h>
#include <linux/mutex.h>
#include <linux/notifier.h>
#include <linux/platform_data/cros_ec_commands.h>

struct iio_dev;

/**
 * struct cros_ec_sensor_platform - ChromeOS EC sensor platform information.
 * @sensor_num: Id of the sensor, as reported by the EC.
 */
struct cros_ec_sensor_platform {
	u8 sensor_num;
};

/**
 * typedef cros_ec_sensorhub_push_data_cb_t - Callback function to send datum
 *					      to specific sensors.
 *
 * @indio_dev: The IIO device that will process the sample.
 * @data: Vector array of the ring sample.
 * @timestamp: Timestamp in host timespace when the sample was acquired by
 *             the EC.
 */
typedef int (*cros_ec_sensorhub_push_data_cb_t)(struct iio_dev *indio_dev,
						s16 *data,
						s64 timestamp);

struct cros_ec_sensorhub_sensor_push_data {
	struct iio_dev *indio_dev;
	cros_ec_sensorhub_push_data_cb_t push_data_cb;
};

enum {
	CROS_EC_SENSOR_LAST_TS,
	CROS_EC_SENSOR_NEW_TS,
	CROS_EC_SENSOR_ALL_TS
};

struct cros_ec_sensors_ring_sample {
	u8  sensor_id;
	u8  flag;
	s16 vector[3];
	s64 timestamp;
} __packed;

/**
 * struct cros_ec_sensorhub - Sensor Hub device data.
 *
 * @dev: Device object, mostly used for logging.
 * @ec: Embedded Controller where the hub is located.
 * @sensor_num: Number of MEMS sensors present in the EC.
 * @msg: Structure to send FIFO requests.
 * @params: Pointer to parameters in msg.
 * @resp: Pointer to responses in msg.
 * @cmd_lock : Lock for sending msg.
 * @notifier: Notifier to kick the FIFO interrupt.
 * @ring: Preprocessed ring to store events.
 * @fifo_timestamp: array for event timestamp and spreading.
 * @fifo_info: copy of FIFO information coming from the EC.
 * @fifo_size: size of the ring.
 * @push_data: array of callback to send datums to iio sensor object.
 */
struct cros_ec_sensorhub {
	struct device *dev;
	struct cros_ec_dev *ec;
	int sensor_num;

	struct cros_ec_command *msg;
	struct ec_params_motion_sense *params;
	struct ec_response_motion_sense *resp;
	struct mutex cmd_lock;  /* Lock for protecting msg structure. */

	struct notifier_block notifier;

	struct cros_ec_sensors_ring_sample *ring;

	ktime_t fifo_timestamp[CROS_EC_SENSOR_ALL_TS];
	struct ec_response_motion_sense_fifo_info *fifo_info;
	int fifo_size;

	struct cros_ec_sensorhub_sensor_push_data *push_data;
};

int cros_ec_sensorhub_register_push_data(struct cros_ec_sensorhub *sensorhub,
					 u8 sensor_num,
					 struct iio_dev *indio_dev,
					 cros_ec_sensorhub_push_data_cb_t cb);

void cros_ec_sensorhub_unregister_push_data(struct cros_ec_sensorhub *sensorhub,
					    u8 sensor_num);

int cros_ec_sensorhub_ring_add(struct cros_ec_sensorhub *sensorhub);
void cros_ec_sensorhub_ring_remove(void *arg);
int cros_ec_sensorhub_ring_fifo_enable(struct cros_ec_sensorhub *sensorhub,
				       bool on);

#endif   /* __LINUX_PLATFORM_DATA_CROS_EC_SENSORHUB_H */
