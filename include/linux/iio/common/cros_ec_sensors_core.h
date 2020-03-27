/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * ChromeOS EC sensor hub
 *
 * Copyright (C) 2016 Google, Inc
 */

#ifndef __CROS_EC_SENSORS_CORE_H
#define __CROS_EC_SENSORS_CORE_H

#include <linux/iio/iio.h>
#include <linux/irqreturn.h>
#include <linux/platform_data/cros_ec_commands.h>
#include <linux/platform_data/cros_ec_proto.h>
#include <linux/platform_data/cros_ec_sensorhub.h>

enum {
	CROS_EC_SENSOR_X,
	CROS_EC_SENSOR_Y,
	CROS_EC_SENSOR_Z,
	CROS_EC_SENSOR_MAX_AXIS,
};

/* EC returns sensor values using signed 16 bit registers */
#define CROS_EC_SENSOR_BITS 16

/*
 * 4 16 bit channels are allowed.
 * Good enough for current sensors, they use up to 3 16 bit vectors.
 */
#define CROS_EC_SAMPLE_SIZE  (sizeof(s64) * 2)

typedef irqreturn_t (*cros_ec_sensors_capture_t)(int irq, void *p);

/**
 * struct cros_ec_sensors_core_state - state data for EC sensors IIO driver
 * @ec:				cros EC device structure
 * @cmd_lock:			lock used to prevent simultaneous access to the
 *				commands.
 * @msg:			cros EC command structure
 * @param:			motion sensor parameters structure
 * @resp:			motion sensor response structure
 * @type:			type of motion sensor
 * @loc:			location where the motion sensor is placed
 * @calib:			calibration parameters. Note that trigger
 *				captured data will always provide the calibrated
 *				data
 * @samples:			static array to hold data from a single capture.
 *				For each channel we need 2 bytes, except for
 *				the timestamp. The timestamp is always last and
 *				is always 8-byte aligned.
 * @read_ec_sensors_data:	function used for accessing sensors values
 * @fifo_max_event_count:	Size of the EC sensor FIFO
 * @frequencies:		Table of known available frequencies:
 *				0, Min and Max in mHz
 */
struct cros_ec_sensors_core_state {
	struct cros_ec_device *ec;
	struct mutex cmd_lock;

	struct cros_ec_command *msg;
	struct ec_params_motion_sense param;
	struct ec_response_motion_sense *resp;

	enum motionsensor_type type;
	enum motionsensor_location loc;

	struct calib_data {
		s16 offset;
		u16 scale;
	} calib[CROS_EC_SENSOR_MAX_AXIS];
	s8 sign[CROS_EC_SENSOR_MAX_AXIS];
	u8 samples[CROS_EC_SAMPLE_SIZE];

	int (*read_ec_sensors_data)(struct iio_dev *indio_dev,
				    unsigned long scan_mask, s16 *data);

	u32 fifo_max_event_count;
	int frequencies[6];
};

int cros_ec_sensors_read_lpc(struct iio_dev *indio_dev, unsigned long scan_mask,
			     s16 *data);

int cros_ec_sensors_read_cmd(struct iio_dev *indio_dev, unsigned long scan_mask,
			     s16 *data);

struct platform_device;
int cros_ec_sensors_core_init(struct platform_device *pdev,
			      struct iio_dev *indio_dev, bool physical_device,
			      cros_ec_sensors_capture_t trigger_capture,
			      cros_ec_sensorhub_push_data_cb_t push_data);

irqreturn_t cros_ec_sensors_capture(int irq, void *p);
int cros_ec_sensors_push_data(struct iio_dev *indio_dev,
			      s16 *data,
			      s64 timestamp);

int cros_ec_motion_send_host_cmd(struct cros_ec_sensors_core_state *st,
				 u16 opt_length);

int cros_ec_sensors_core_read(struct cros_ec_sensors_core_state *st,
			      struct iio_chan_spec const *chan,
			      int *val, int *val2, long mask);

int cros_ec_sensors_core_read_avail(struct iio_dev *indio_dev,
				    struct iio_chan_spec const *chan,
				    const int **vals,
				    int *type,
				    int *length,
				    long mask);

int cros_ec_sensors_core_write(struct cros_ec_sensors_core_state *st,
			       struct iio_chan_spec const *chan,
			       int val, int val2, long mask);

/* List of extended channel specification for all sensors */
extern const struct iio_chan_spec_ext_info cros_ec_sensors_ext_info[];
extern const struct attribute *cros_ec_sensor_fifo_attributes[];

#endif  /* __CROS_EC_SENSORS_CORE_H */
