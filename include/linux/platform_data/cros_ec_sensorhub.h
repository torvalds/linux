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

/* State used for cros_ec_ring_fix_overflow */
struct cros_ec_sensors_ec_overflow_state {
	s64 offset;
	s64 last;
};

/* Length of the filter, how long to remember entries for */
#define CROS_EC_SENSORHUB_TS_HISTORY_SIZE 64

/**
 * struct cros_ec_sensors_ts_filter_state - Timestamp filetr state.
 *
 * @x_offset: x is EC interrupt time. x_offset its last value.
 * @y_offset: y is the difference between AP and EC time, y_offset its last
 *            value.
 * @x_history: The past history of x, relative to x_offset.
 * @y_history: The past history of y, relative to y_offset.
 * @m_history: rate between y and x.
 * @history_len: Amount of valid historic data in the arrays.
 * @temp_buf: Temporary buffer used when updating the filter.
 * @median_m: median value of m_history
 * @median_error: final error to apply to AP interrupt timestamp to get the
 *                "true timestamp" the event occurred.
 */
struct cros_ec_sensors_ts_filter_state {
	s64 x_offset, y_offset;
	s64 x_history[CROS_EC_SENSORHUB_TS_HISTORY_SIZE];
	s64 y_history[CROS_EC_SENSORHUB_TS_HISTORY_SIZE];
	s64 m_history[CROS_EC_SENSORHUB_TS_HISTORY_SIZE];
	int history_len;

	s64 temp_buf[CROS_EC_SENSORHUB_TS_HISTORY_SIZE];

	s64 median_m;
	s64 median_error;
};

/* struct cros_ec_sensors_ts_batch_state - State of batch of a single sensor.
 *
 * Use to store information to batch data using median fileter information.
 *
 * @penul_ts: last but one batch timestamp (penultimate timestamp).
 *	      Used for timestamp spreading calculations
 *	      when a batch shows up.
 * @penul_len: last but one batch length.
 * @last_ts: Last batch timestam.
 * @last_len: Last batch length.
 * @newest_sensor_event: Last sensor timestamp.
 */
struct cros_ec_sensors_ts_batch_state {
	s64 penul_ts;
	int penul_len;
	s64 last_ts;
	int last_len;
	s64 newest_sensor_event;
};

/*
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
 * @fifo_timestamp: Array for event timestamp and spreading.
 * @fifo_info: Copy of FIFO information coming from the EC.
 * @fifo_size: Size of the ring.
 * @batch_state: Per sensor information of the last batches received.
 * @overflow_a: For handling timestamp overflow for a time (sensor events)
 * @overflow_b: For handling timestamp overflow for b time (ec interrupts)
 * @filter: Medium fileter structure.
 * @tight_timestamps: Set to truen when EC support tight timestamping:
 *		      The timestamps reported from the EC have low jitter.
 *		      Timestamps also come before every sample. Set either
 *		      by feature bits coming from the EC or userspace.
 * @future_timestamp_count: Statistics used to compute shaved time.
 *			    This occurs when timestamp interpolation from EC
 *			    time to AP time accidentally puts timestamps in
 *			    the future. These timestamps are clamped to
 *			    `now` and these count/total_ns maintain the
 *			    statistics for how much time was removed in a
 *			    given period.
 * @future_timestamp_total_ns: Total amount of time shaved.
 * @push_data: Array of callback to send datums to iio sensor object.
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

	struct cros_ec_sensors_ts_batch_state *batch_state;

	struct cros_ec_sensors_ec_overflow_state overflow_a;
	struct cros_ec_sensors_ec_overflow_state overflow_b;

	struct cros_ec_sensors_ts_filter_state filter;

	int tight_timestamps;

	s32 future_timestamp_count;
	s64 future_timestamp_total_ns;

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
