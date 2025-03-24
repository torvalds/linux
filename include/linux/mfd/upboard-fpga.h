/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * UP Board CPLD/FPGA driver
 *
 * Copyright (c) AAEON. All rights reserved.
 * Copyright (C) 2024 Bootlin
 *
 * Author: Gary Wang <garywang@aaeon.com.tw>
 * Author: Thomas Richard <thomas.richard@bootlin.com>
 *
 */

#ifndef __LINUX_MFD_UPBOARD_FPGA_H
#define __LINUX_MFD_UPBOARD_FPGA_H

#define UPBOARD_REGISTER_SIZE 16

enum upboard_fpgareg {
	UPBOARD_REG_PLATFORM_ID   = 0x10,
	UPBOARD_REG_FIRMWARE_ID   = 0x11,
	UPBOARD_REG_FUNC_EN0      = 0x20,
	UPBOARD_REG_FUNC_EN1      = 0x21,
	UPBOARD_REG_GPIO_EN0      = 0x30,
	UPBOARD_REG_GPIO_EN1      = 0x31,
	UPBOARD_REG_GPIO_EN2      = 0x32,
	UPBOARD_REG_GPIO_DIR0     = 0x40,
	UPBOARD_REG_GPIO_DIR1     = 0x41,
	UPBOARD_REG_GPIO_DIR2     = 0x42,
	UPBOARD_REG_MAX,
};

enum upboard_fpga_type {
	UPBOARD_UP_FPGA,
	UPBOARD_UP2_FPGA,
};

struct upboard_fpga_data {
	enum upboard_fpga_type type;
	const struct regmap_config *regmap_config;
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
	unsigned int firmware_version;
	const struct upboard_fpga_data *fpga_data;
};

#endif /*  __LINUX_MFD_UPBOARD_FPGA_H */
