/*
 * Functions to access Nintendo 3DS MCU chip.
 *
 * Copyright (C) 2016 Sergi Granell <xerpi.g.12@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __LINUX_MFD_NINTENDO3DS_MCU_H
#define __LINUX_MFD_NINTENDO3DS_MCU_H

#include <linux/mutex.h>
#include <linux/pm.h>


/* MCU register map */
#define NINTENDO3DS_MCU_REG_PWRCTL	0x20
#define NINTENDO3DS_MCU_REG_RTC		0x30


struct nintendo3ds_mcu_dev {
	struct device *dev;
	struct i2c_client *i2c_client;
	int (*read_device)(struct nintendo3ds_mcu_dev *mcu, char reg, int size,
			void *dest);
	int (*write_device)(struct nintendo3ds_mcu_dev *mcu, char reg, int size,
			 void *src);
};

#endif
