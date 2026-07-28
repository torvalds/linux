/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LINUX_DEVICE_ID_I2C_H
#define LINUX_DEVICE_ID_I2C_H

#ifdef __KERNEL__
typedef unsigned long kernel_ulong_t;
#endif

/* i2c */

#define I2C_NAME_SIZE	20
#define I2C_MODULE_PREFIX "i2c:"

struct i2c_device_id {
	char name[I2C_NAME_SIZE];
	kernel_ulong_t driver_data;	/* Data private to the driver */
};

#endif /* ifndef LINUX_DEVICE_ID_I2C_H */
