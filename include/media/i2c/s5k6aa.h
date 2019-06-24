/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * S5K6AAFX camera sensor driver header
 *
 * Copyright (C) 2011 Samsung Electronics Co., Ltd.
 */

#ifndef S5K6AA_H
#define S5K6AA_H

#include <media/v4l2-mediabus.h>

/**
 * struct s5k6aa_gpio - data structure describing a GPIO
 * @gpio:  GPIO number
 * @level: indicates active state of the @gpio
 */
struct s5k6aa_gpio {
	int gpio;
	int level;
};

/**
 * struct s5k6aa_platform_data - s5k6aa driver platform data
 * @set_power:   an additional callback to the board code, called
 *               after enabling the regulators and before switching
 *               the sensor off
 * @mclk_frequency: sensor's master clock frequency in Hz
 * @gpio_reset:  GPIO driving RESET pin
 * @gpio_stby:   GPIO driving STBY pin
 * @nlanes:      maximum number of MIPI-CSI lanes used
 * @horiz_flip:  default horizontal image flip value, non zero to enable
 * @vert_flip:   default vertical image flip value, non zero to enable
 */

struct s5k6aa_platform_data {
	int (*set_power)(int enable);
	unsigned long mclk_frequency;
	struct s5k6aa_gpio gpio_reset;
	struct s5k6aa_gpio gpio_stby;
	enum v4l2_mbus_type bus_type;
	u8 nlanes;
	u8 horiz_flip;
	u8 vert_flip;
};

#endif /* S5K6AA_H */
