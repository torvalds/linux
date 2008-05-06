/*
 * v4l2-i2c-drv-legacy.h - contains I2C handling code that's identical
 *		    for all V4L2 I2C drivers. Use this header if the
 *		    I2C driver is used by both legacy drivers and
 *		    drivers converted to the bus-based I2C API.
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

struct v4l2_i2c_driver_data {
	const char * const name;
	int driverid;
	int (*command)(struct i2c_client *client, unsigned int cmd, void *arg);
	int (*probe)(struct i2c_client *client, const struct i2c_device_id *id);
	int (*remove)(struct i2c_client *client);
	int (*suspend)(struct i2c_client *client, pm_message_t state);
	int (*resume)(struct i2c_client *client);
	int (*legacy_probe)(struct i2c_adapter *adapter);
	int legacy_class;
};

static struct v4l2_i2c_driver_data v4l2_i2c_data;
static const struct i2c_client_address_data addr_data;
static struct i2c_driver v4l2_i2c_driver_legacy;
static char v4l2_i2c_drv_name_legacy[32];

static int v4l2_i2c_drv_attach_legacy(struct i2c_adapter *adapter, int address, int kind)
{
	return v4l2_i2c_attach(adapter, address, &v4l2_i2c_driver_legacy,
			v4l2_i2c_drv_name_legacy, v4l2_i2c_data.probe);
}

static int v4l2_i2c_drv_probe_legacy(struct i2c_adapter *adapter)
{
	if (v4l2_i2c_data.legacy_probe) {
		if (v4l2_i2c_data.legacy_probe(adapter))
			return i2c_probe(adapter, &addr_data, v4l2_i2c_drv_attach_legacy);
		return 0;
	}
	if (adapter->class & v4l2_i2c_data.legacy_class)
		return i2c_probe(adapter, &addr_data, v4l2_i2c_drv_attach_legacy);
	return 0;
}

static int v4l2_i2c_drv_detach_legacy(struct i2c_client *client)
{
	int err;

	if (v4l2_i2c_data.remove)
		v4l2_i2c_data.remove(client);

	err = i2c_detach_client(client);
	if (err)
		return err;
	kfree(client);

	return 0;
}

static int v4l2_i2c_drv_suspend_helper(struct i2c_client *client, pm_message_t state)
{
	return v4l2_i2c_data.suspend ? v4l2_i2c_data.suspend(client, state) : 0;
}

static int v4l2_i2c_drv_resume_helper(struct i2c_client *client)
{
	return v4l2_i2c_data.resume ? v4l2_i2c_data.resume(client) : 0;
}

/* ----------------------------------------------------------------------- */

/* i2c implementation */
static struct i2c_driver v4l2_i2c_driver_legacy = {
	.driver = {
		.owner = THIS_MODULE,
	},
	.attach_adapter = v4l2_i2c_drv_probe_legacy,
	.detach_client = v4l2_i2c_drv_detach_legacy,
	.suspend = v4l2_i2c_drv_suspend_helper,
	.resume  = v4l2_i2c_drv_resume_helper,
};

/* ----------------------------------------------------------------------- */

/* i2c implementation */
static struct i2c_driver v4l2_i2c_driver = {
	.suspend 	= v4l2_i2c_drv_suspend_helper,
	.resume  	= v4l2_i2c_drv_resume_helper,
};

static int __init v4l2_i2c_drv_init(void)
{
	int err;

	strlcpy(v4l2_i2c_drv_name_legacy, v4l2_i2c_data.name, sizeof(v4l2_i2c_drv_name_legacy));
	strlcat(v4l2_i2c_drv_name_legacy, "'", sizeof(v4l2_i2c_drv_name_legacy));

	if (v4l2_i2c_data.legacy_class == 0)
		v4l2_i2c_data.legacy_class = I2C_CLASS_TV_ANALOG;

	v4l2_i2c_driver_legacy.driver.name = v4l2_i2c_drv_name_legacy;
	v4l2_i2c_driver_legacy.id = v4l2_i2c_data.driverid;
	v4l2_i2c_driver_legacy.command = v4l2_i2c_data.command;
	err = i2c_add_driver(&v4l2_i2c_driver_legacy);

	if (err)
		return err;
	v4l2_i2c_driver.driver.name = v4l2_i2c_data.name;
	v4l2_i2c_driver.id = v4l2_i2c_data.driverid;
	v4l2_i2c_driver.command = v4l2_i2c_data.command;
	v4l2_i2c_driver.probe = v4l2_i2c_data.probe;
	v4l2_i2c_driver.remove = v4l2_i2c_data.remove;
	err = i2c_add_driver(&v4l2_i2c_driver);
	if (err)
		i2c_del_driver(&v4l2_i2c_driver_legacy);
	return err;
}

static void __exit v4l2_i2c_drv_cleanup(void)
{
	i2c_del_driver(&v4l2_i2c_driver_legacy);
	i2c_del_driver(&v4l2_i2c_driver);
}

module_init(v4l2_i2c_drv_init);
module_exit(v4l2_i2c_drv_cleanup);
