/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Chrome OS EC MEMS Sensor Hub driver.
 *
 * Copyright 2019 Google LLC
 */

#ifndef __LINUX_PLATFORM_DATA_CROS_EC_SENSORHUB_H
#define __LINUX_PLATFORM_DATA_CROS_EC_SENSORHUB_H

#include <linux/platform_data/cros_ec_commands.h>

/**
 * struct cros_ec_sensor_platform - ChromeOS EC sensor platform information.
 * @sensor_num: Id of the sensor, as reported by the EC.
 */
struct cros_ec_sensor_platform {
	u8 sensor_num;
};

/**
 * struct cros_ec_sensorhub - Sensor Hub device data.
 *
 * @ec: Embedded Controller where the hub is located.
 */
struct cros_ec_sensorhub {
	struct cros_ec_dev *ec;
};

#endif   /* __LINUX_PLATFORM_DATA_CROS_EC_SENSORHUB_H */
