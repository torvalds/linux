/*
 * Copyright (c) 2017 Rockchip Electronics Co. Ltd.
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
 */

#ifndef __RK618_H__
#define __RK618_H__

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/regmap.h>

struct rk618 {
	struct device *dev;
	struct i2c_client *client;
	struct clk *clkin;
	struct regmap *regmap;

	struct regulator *supply;
	struct gpio_desc *enable_gpio;
	struct gpio_desc *reset_gpio;	/* power on reset */
};

#endif
