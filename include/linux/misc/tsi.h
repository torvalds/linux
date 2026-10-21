/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * AMD SBTSI shared data structure and auxiliary bus definitions.
 *
 * Copyright (C) 2026 Advanced Micro Devices, Inc.
 */

#ifndef _LINUX_MISC_TSI_H_
#define _LINUX_MISC_TSI_H_

#include <linux/cleanup.h>
#include <linux/i2c.h>
#include <linux/i3c/device.h>
#include <linux/kref.h>
#include <linux/miscdevice.h>
#include <linux/mutex.h>
#include <linux/types.h>

/**
 * struct sbtsi_data - driver private data for an AMD SB-TSI device
 * @client:	underlying I2C client
 * @i3cdev:	underlying I3C device (when using I3C bus)
 * @sbtsi_misc_dev: miscdevice exposing ioctl interface at /dev/sbtsi-<addr>
 * @lock:           mutex protecting concurrent access to the device
 * @kref:      reference count; keeps @sbtsi_data alive while misc fds are open
 * @dev_addr:	I2C/I3C device address, used as the auxiliary device instance id
 *		and name the misc device node
 * @ext_range_mode:	sensor uses extended temperature range
 * @read_order:	if set, decimal part must be read before integer part
 * @is_i3c:	true when the device is accessed over I3C
 * @detached:  set on driver unbind; open/ioctl return -ENODEV afterward
 */
struct sbtsi_data {
	union {
		struct i2c_client *client;
		struct i3c_device *i3cdev;
	};
	struct miscdevice sbtsi_misc_dev;
	struct mutex lock;	/* protects concurrent access to the device */
	struct kref kref;
	u8 dev_addr;
	bool ext_range_mode;
	bool read_order;
	bool is_i3c;
	bool detached;
};

DEFINE_GUARD(sbtsi, struct sbtsi_data *, mutex_lock(&_T->lock),
	     mutex_unlock(&_T->lock))

/*
 * Name of the auxiliary device published on the auxiliary bus by the core
 * driver.  The full device name is "amd-sbtsi.temp-sensor.<id>". where
 * <id> is the auxiliary device instance id.
 */
#define AMD_SBTSI_ADEV		"amd-sbtsi"
#define AMD_SBTSI_AUX_HWMON	"temp-sensor"

/**
 * sbtsi_xfer - Perform a register read or write transfer on an AMD SB-TSI device.
 *
 * @data:    Pointer to the sbtsi_data structure containing the device context
 * @reg:     Register address to access.
 * @val:     Pointer to the value to read into or write from.
 * @is_read: If true, performs a read transfer and stores the result in @val.
 *           If false, performs a write transfer using the value in @val.
 *
 * Returns 0 on success, or a negative error code on failure.
 */
int sbtsi_xfer(struct sbtsi_data *data, u8 reg, u8 *val, bool is_read);

#endif /* _LINUX_MISC_TSI_H_ */
