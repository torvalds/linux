/*
 * Copyright (C) 2010 MEMSIC, Inc.
 *
 * Initial Code:
 *	Robbie Cao
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */

/*
 * Definitions for mma8452 accelorometer sensor chip.
 */
#ifndef __MMA8452_H__
#define __MMA8452_H__

#include <linux/ioctl.h>

#define MMA8452_I2C_NAME		"mma8452"

/*
 * This address comes must match the part# on your target.
 * Address to the sensor part# support as following list:
 *   MMA8452	- 0x1C
 * Please refer to sensor datasheet for detail.
 */
#define MMA8452_I2C_ADDR		0x1c

/* MMA8452 register address */
#define MMA8452_REG_CTRL		0x2a
#define MMA8452_REG_DATA		0x01
#define MMA8452_XYZ_DATA_CFG		0x0e
/* MMA8452 control bit */
#define MMA8452_CTRL_PWRON_1		0x20	/* acceleration samples 20ms */
#define MMA8452_CTRL_PWRON_2		0x28	/* acceleration samples 80ms */
#define MMA8452_CTRL_PWRON_3		0x30	/* acceleration samples 160ms */
#define MMA8452_CTRL_PWRON_4		0x38	/* acceleration samples 640ms */
#define MMA8452_CTRL_PWRDN		0x80	/* power donw */

#define MMA8452_CTRL_MODE_2G		0x00
#define MMA8452_CTRL_MODE_4G		0x01
#define MMA8452_CTRL_MODE_8G		0x02
#define MMA8452_CTRL_ACTIVE		0x01	/* ACTIVE */



/* Use 'm' as magic number */
#define MMA8452_IOM			'm'

/* IOCTLs for MMA8452 device */
#define MMA8452_IOC_PWRON		_IO (MMA8452_IOM, 0x00)
#define MMA8452_IOC_PWRDN		_IO (MMA8452_IOM, 0x01)
#define MMA8452_IOC_READXYZ		_IOR(MMA8452_IOM, 0x05, int[3])
#define MMA8452_IOC_READSTATUS		_IOR(MMA8452_IOM, 0x07, int[3])
#define MMA8452_IOC_SETDETECTION	_IOW(MMA8452_IOM, 0x08, unsigned char)

#endif /* __MMA8452_H__ */

