// SPDX-License-Identifier: GPL-2.0
/*
 * NXP ISP1301 USB transceiver driver
 *
 * Copyright (C) 2012 Roland Stigge <stigge@antcom.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef __LINUX_USB_ISP1301_H
#define __LINUX_USB_ISP1301_H

#include <linux/of.h>

/* I2C Register definitions: */

#define ISP1301_I2C_MODE_CONTROL_1	0x04	/* u8 read, set, +1 clear */

#define MC1_SPEED_REG			(1 << 0)
#define MC1_SUSPEND_REG			(1 << 1)
#define MC1_DAT_SE0			(1 << 2)
#define MC1_TRANSPARENT			(1 << 3)
#define MC1_BDIS_ACON_EN		(1 << 4)
#define MC1_OE_INT_EN			(1 << 5)
#define MC1_UART_EN			(1 << 6)
#define MC1_MASK			0x7f

#define ISP1301_I2C_MODE_CONTROL_2	0x12	/* u8 read, set, +1 clear */

#define MC2_GLOBAL_PWR_DN		(1 << 0)
#define MC2_SPD_SUSP_CTRL		(1 << 1)
#define MC2_BI_DI			(1 << 2)
#define MC2_TRANSP_BDIR0		(1 << 3)
#define MC2_TRANSP_BDIR1		(1 << 4)
#define MC2_AUDIO_EN			(1 << 5)
#define MC2_PSW_EN			(1 << 6)
#define MC2_EN2V7			(1 << 7)

#define ISP1301_I2C_OTG_CONTROL_1	0x06	/* u8 read, set, +1 clear */

#define OTG1_DP_PULLUP			(1 << 0)
#define OTG1_DM_PULLUP			(1 << 1)
#define OTG1_DP_PULLDOWN		(1 << 2)
#define OTG1_DM_PULLDOWN		(1 << 3)
#define OTG1_ID_PULLDOWN		(1 << 4)
#define OTG1_VBUS_DRV			(1 << 5)
#define OTG1_VBUS_DISCHRG		(1 << 6)
#define OTG1_VBUS_CHRG			(1 << 7)

#define ISP1301_I2C_OTG_CONTROL_2	0x10	/* u8 readonly */

#define OTG_B_SESS_END			(1 << 6)
#define OTG_B_SESS_VLD			(1 << 7)

#define ISP1301_I2C_INTERRUPT_SOURCE	0x8
#define ISP1301_I2C_INTERRUPT_LATCH	0xA
#define ISP1301_I2C_INTERRUPT_FALLING	0xC
#define ISP1301_I2C_INTERRUPT_RISING	0xE

#define INT_VBUS_VLD			(1 << 0)
#define INT_SESS_VLD			(1 << 1)
#define INT_DP_HI			(1 << 2)
#define INT_ID_GND			(1 << 3)
#define INT_DM_HI			(1 << 4)
#define INT_ID_FLOAT			(1 << 5)
#define INT_BDIS_ACON			(1 << 6)
#define INT_CR_INT			(1 << 7)

#define ISP1301_I2C_REG_CLEAR_ADDR	1	/* Register Address Modifier */

struct i2c_client *isp1301_get_client(struct device_node *node);

#endif /* __LINUX_USB_ISP1301_H */
