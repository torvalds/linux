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
#include <linux/mfd/cros_ec.h>

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

/* Minimum sampling period to use when device is suspending */
#define CROS_EC_MIN_SUSPEND_SAMPLING_FREQUENCY 1000  /* 1 second */

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
 * @cuur_sampl_freq:		current sampling period
 */
struct cros_ec_sensors_core_state {
	struct cros_ec_device *ec;
	struct mutex cmd_lock;

	struct cros_ec_command *msg;
	struct ec_params_motion_sense param;
	struct ec_response_motion_sense *resp;

	enum motionsensor_type type;
	enum motionsensor_location loc;

	s16 calib[CROS_EC_SENSOR_MAX_AXIS];

	u8 samples[CROS_EC_SAMPLE_SIZE];

	int (*read_ec_sensors_data)(struct iio_dev *indio_dev,
				    unsigned long scan_mask, s16 *data);

	int curr_sampl_freq;
};

/**
 * cros_ec_sensors_read_lpc() - retrieve data from EC shared memory
 * @indio_dev:	pointer to IIO device
 * @scan_mask:	bitmap of the sensor indices to scan
 * @data:	location to store data
 *
 * This is the safe function for reading the EC data. It guarantees that the
 * data sampled was not modified by the EC while being read.
 *
 * Return: 0 on success, -errno on failure.
 */
int cros_ec_sensors_read_lpc(struct iio_dev *indio_dev, unsigned long scan_mask,
			     s16 *data);

/**
 * cros_ec_sensors_read_cmd() - retrieve data using the EC command protocol
 * @indio_dev:	pointer to IIO device
 * @scan_mask:	bitmap of the sensor indices to scan
 * @data:	location to store data
 *
 * Return: 0 on success, -errno on failure.
 */
int cros_ec_sensors_read_cmd(struct iio_dev *indio_dev, unsigned long scan_mask,
			     s16 *data);

struct platform_device;
/**
 * cros_ec_sensors_core_init() - basic initialization of the core structure
 * @pdev:		platform device created for the sensors
 * @indio_dev:		iio device structure of the device
 * @physical_device:	true if the device refers to a physical device
 *
 * Return: 0 on success, -errno on failure.
 */
int cros_ec_sensors_core_init(struct platform_device *pdev,
			      struct iio_dev *indio_dev, bool physical_device);

/**
 * cros_ec_sensors_capture() - the trigger handler function
 * @irq:	the interrupt number.
 * @p:		a pointer to the poll function.
 *
 * On a trigger event occurring, if the pollfunc is attached then this
 * handler is called as a threaded interrupt (and hence may sleep). It
 * is responsible for grabbing data from the device and pushing it into
 * the associated buffer.
 *
 * Return: IRQ_HANDLED
 */
irqreturn_t cros_ec_sensors_capture(int irq, void *p);

/**
 * cros_ec_motion_send_host_cmd() - send motion sense host command
 * @st:		pointer to state information for device
 * @opt_length:	optional length to reduce the response size, useful on the data
 *		path. Otherwise, the maximal allowed response size is used
 *
 * When called, the sub-command is assumed to be set in param->cmd.
 *
 * Return: 0 on success, -errno on failure.
 */
int cros_ec_motion_send_host_cmd(struct cros_ec_sensors_core_state *st,
				 u16 opt_length);

/**
 * cros_ec_sensors_core_read() - function to request a value from the sensor
 * @st:		pointer to state information for device
 * @chan:	channel specification structure table
 * @val:	will contain one element making up the returned value
 * @val2:	will contain another element making up the returned value
 * @mask:	specifies which values to be requested
 *
 * Return:	the type of value returned by the device
 */
int cros_ec_sensors_core_read(struct cros_ec_sensors_core_state *st,
			      struct iio_chan_spec const *chan,
			      int *val, int *val2, long mask);

/**
 * cros_ec_sensors_core_write() - function to write a value to the sensor
 * @st:		pointer to state information for device
 * @chan:	channel specification structure table
 * @val:	first part of value to write
 * @val2:	second part of value to write
 * @mask:	specifies which values to write
 *
 * Return:	the type of value returned by the device
 */
int cros_ec_sensors_core_write(struct cros_ec_sensors_core_state *st,
			       struct iio_chan_spec const *chan,
			       int val, int val2, long mask);

extern const struct dev_pm_ops cros_ec_sensors_pm_ops;

/* List of extended channel specification for all sensors */
extern const struct iio_chan_spec_ext_info cros_ec_sensors_ext_info[];

#endif  /* __CROS_EC_SENSORS_CORE_H */
