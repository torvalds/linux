/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * s3c24xx/s3c64xx SoC series Camera Interface (CAMIF) driver
 *
 * Copyright (C) 2012 Sylwester Nawrocki <sylvester.nawrocki@gmail.com>
*/

#ifndef MEDIA_S3C_CAMIF_
#define MEDIA_S3C_CAMIF_

#include <linux/i2c.h>
#include <media/v4l2-mediabus.h>

/**
 * struct s3c_camif_sensor_info - an image sensor description
 * @i2c_board_info: pointer to an I2C sensor subdevice board info
 * @clock_frequency: frequency of the clock the host provides to a sensor
 * @mbus_type: media bus type
 * @i2c_bus_num: i2c control bus id the sensor is attached to
 * @flags: the parallel bus flags defining signals polarity (V4L2_MBUS_*)
 * @use_field: 1 if parallel bus FIELD signal is used (only s3c64xx)
 */
struct s3c_camif_sensor_info {
	struct i2c_board_info i2c_board_info;
	unsigned long clock_frequency;
	enum v4l2_mbus_type mbus_type;
	u16 i2c_bus_num;
	u16 flags;
	u8 use_field;
};

struct s3c_camif_plat_data {
	struct s3c_camif_sensor_info sensor;
	int (*gpio_get)(void);
	int (*gpio_put)(void);
};

/* Platform default helper functions */
int s3c_camif_gpio_get(void);
int s3c_camif_gpio_put(void);

#endif /* MEDIA_S3C_CAMIF_ */
