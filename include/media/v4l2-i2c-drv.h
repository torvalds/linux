/*
 * v4l2-i2c-drv.h - contains I2C handling code that's identical for
 *		    all V4L2 I2C drivers. Use this header if the
 *		    I2C driver is only used by drivers converted
 *		    to the bus-based I2C API.
 *
 * Copyright (C) 2007 Hans Verkuil <hverkuil@xs4all.nl>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/* NOTE: the full version of this header is in the v4l-dvb repository
 * and allows v4l i2c drivers to be compiled on older kernels as well.
 * The version of this header as it appears in the kernel is a stripped
 * version (without all the backwards compatibility stuff) and so it
 * looks a bit odd.
 *
 * If you look at the full version then you will understand the reason
 * for introducing this header since you really don't want to have all
 * the tricky backwards compatibility code in each and every i2c driver.
 */

#ifndef __V4L2_I2C_DRV_H__
#define __V4L2_I2C_DRV_H__

#include <media/v4l2-common.h>

struct v4l2_i2c_driver_data {
	const char * const name;
	int (*command)(struct i2c_client *client, unsigned int cmd, void *arg);
	int (*probe)(struct i2c_client *client, const struct i2c_device_id *id);
	int (*remove)(struct i2c_client *client);
	int (*suspend)(struct i2c_client *client, pm_message_t state);
	int (*resume)(struct i2c_client *client);
	const struct i2c_device_id *id_table;
};

static struct v4l2_i2c_driver_data v4l2_i2c_data;
static struct i2c_driver v4l2_i2c_driver;


/* Bus-based I2C implementation for kernels >= 2.6.26 */

static int __init v4l2_i2c_drv_init(void)
{
	v4l2_i2c_driver.driver.name = v4l2_i2c_data.name;
	v4l2_i2c_driver.command = v4l2_i2c_data.command;
	v4l2_i2c_driver.probe = v4l2_i2c_data.probe;
	v4l2_i2c_driver.remove = v4l2_i2c_data.remove;
	v4l2_i2c_driver.suspend = v4l2_i2c_data.suspend;
	v4l2_i2c_driver.resume = v4l2_i2c_data.resume;
	v4l2_i2c_driver.id_table = v4l2_i2c_data.id_table;
	return i2c_add_driver(&v4l2_i2c_driver);
}


static void __exit v4l2_i2c_drv_cleanup(void)
{
	i2c_del_driver(&v4l2_i2c_driver);
}

module_init(v4l2_i2c_drv_init);
module_exit(v4l2_i2c_drv_cleanup);

#endif /* __V4L2_I2C_DRV_H__ */
