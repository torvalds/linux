/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * pps-gpio.h -- PPS client for GPIOs
 *
 * Copyright (C) 2011 James Nuss <jamesnuss@nanometrics.ca>
 */

#ifndef _PPS_GPIO_H
#define _PPS_GPIO_H

struct pps_gpio_platform_data {
	struct gpio_desc *gpio_pin;
	struct gpio_desc *echo_pin;
	bool assert_falling_edge;
	bool capture_clear;
	unsigned int echo_active_ms;
};

#endif /* _PPS_GPIO_H */
