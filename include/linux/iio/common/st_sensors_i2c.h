/*
 * STMicroelectronics sensors i2c library driver
 *
 * Copyright 2012-2013 STMicroelectronics Inc.
 *
 * Denis Ciocca <denis.ciocca@st.com>
 *
 * Licensed under the GPL-2.
 */

#ifndef ST_SENSORS_I2C_H
#define ST_SENSORS_I2C_H

#include <linux/i2c.h>
#include <linux/iio/common/st_sensors.h>
#include <linux/of.h>

void st_sensors_i2c_configure(struct iio_dev *indio_dev,
		struct i2c_client *client, struct st_sensor_data *sdata);

#ifdef CONFIG_OF
void st_sensors_of_i2c_probe(struct i2c_client *client,
			     const struct of_device_id *match);
#else
static inline void st_sensors_of_i2c_probe(struct i2c_client *client,
					   const struct of_device_id *match)
{
}
#endif

#endif /* ST_SENSORS_I2C_H */
