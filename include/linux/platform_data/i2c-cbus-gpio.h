/*
 * i2c-cbus-gpio.h - CBUS I2C platform_data definition
 *
 * Copyright (C) 2004-2009 Nokia Corporation
 *
 * Written by Felipe Balbi and Aaro Koskinen.
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License. See the file "COPYING" in the main directory of this
 * archive for more details.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#ifndef __INCLUDE_LINUX_I2C_CBUS_GPIO_H
#define __INCLUDE_LINUX_I2C_CBUS_GPIO_H

struct i2c_cbus_platform_data {
	int dat_gpio;
	int clk_gpio;
	int sel_gpio;
};

#endif /* __INCLUDE_LINUX_I2C_CBUS_GPIO_H */
