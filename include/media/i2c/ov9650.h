/*
 * OV9650/OV9652 camera sensors driver
 *
 * Copyright (C) 2013 Sylwester Nawrocki <sylvester.nawrocki@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef OV9650_H_
#define OV9650_H_

/**
 * struct ov9650_platform_data - ov9650 driver platform data
 * @mclk_frequency: the sensor's master clock frequency in Hz
 * @gpio_pwdn:	    number of a GPIO connected to OV965X PWDN pin
 * @gpio_reset:     number of a GPIO connected to OV965X RESET pin
 *
 * If any of @gpio_pwdn or @gpio_reset are unused then they should be
 * set to a negative value. @mclk_frequency must always be specified.
 */
struct ov9650_platform_data {
	unsigned long mclk_frequency;
	int gpio_pwdn;
	int gpio_reset;
};
#endif /* OV9650_H_ */
