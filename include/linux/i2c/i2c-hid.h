/*
 * HID over I2C protocol implementation
 *
 * Copyright (c) 2012 Benjamin Tissoires <benjamin.tissoires@gmail.com>
 * Copyright (c) 2012 Ecole Nationale de l'Aviation Civile, France
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive for
 * more details.
 */

#ifndef __LINUX_I2C_HID_H
#define __LINUX_I2C_HID_H

#include <linux/types.h>

/**
 * struct i2chid_platform_data - used by hid over i2c implementation.
 * @hid_descriptor_address: i2c register where the HID descriptor is stored.
 *
 * Note that it is the responsibility of the platform driver (or the acpi 5.0
 * driver) to setup the irq related to the gpio in the struct i2c_board_info.
 * The platform driver should also setup the gpio according to the device:
 *
 * A typical example is the following:
 *	irq = gpio_to_irq(intr_gpio);
 *	hkdk4412_i2c_devs5[0].irq = irq; // store the irq in i2c_board_info
 *	gpio_request(intr_gpio, "elan-irq");
 *	s3c_gpio_setpull(intr_gpio, S3C_GPIO_PULL_UP);
 */
struct i2c_hid_platform_data {
	u16 hid_descriptor_address;
};

#endif /* __LINUX_I2C_HID_H */
