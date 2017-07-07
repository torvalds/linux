/*
 * UP Board FPGA MFD driver interface
 *
 * Copyright (c) 2017, Emutex Ltd. All rights reserved.
 *
 * Author: Javier Arteaga <javier@emutex.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __LINUX_MFD_UPBOARD_FPGA_H
#define __LINUX_MFD_UPBOARD_FPGA_H

#define UPFPGA_ADDRESS_SIZE  7
#define UPFPGA_REGISTER_SIZE 16

#define UPFPGA_READ_FLAG     (1 << UPFPGA_ADDRESS_SIZE)

enum upboard_fpgareg {
	UPFPGA_REG_PLATFORM_ID   = 0x10,
	UPFPGA_REG_FIRMWARE_ID   = 0x11,
	UPFPGA_REG_FUNC_EN0      = 0x20,
	UPFPGA_REG_FUNC_EN1      = 0x21,
	UPFPGA_REG_GPIO_EN0      = 0x30,
	UPFPGA_REG_GPIO_EN1      = 0x31,
	UPFPGA_REG_GPIO_EN2      = 0x32,
	UPFPGA_REG_GPIO_DIR0     = 0x40,
	UPFPGA_REG_GPIO_DIR1     = 0x41,
	UPFPGA_REG_GPIO_DIR2     = 0x42,
	UPFPGA_REG_MAX,
};

struct upboard_fpga {
	struct device *dev;
	struct regmap *regmap;
	struct gpio_desc *enable_gpio;
	struct gpio_desc *reset_gpio;
	struct gpio_desc *clear_gpio;
	struct gpio_desc *strobe_gpio;
	struct gpio_desc *datain_gpio;
	struct gpio_desc *dataout_gpio;
	bool uninitialised;
};

struct upboard_led_data {
	unsigned int bit;
	const char *colour;
};

#endif /*  __LINUX_MFD_UPBOARD_FPGA_H */
