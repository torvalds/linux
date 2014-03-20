/*
 * Pinctrl Driver for ADI GPIO2 controller
 *
 * Copyright 2007-2013 Analog Devices Inc.
 *
 * Licensed under the GPLv2 or later
 */


#ifndef PINCTRL_ADI2_H
#define PINCTRL_ADI2_H

#include <linux/io.h>
#include <linux/platform_device.h>

/**
 * struct adi_pinctrl_gpio_platform_data - Pinctrl gpio platform data
 * for ADI GPIO2 device.
 *
 * @port_gpio_base: Optional global GPIO index of the GPIO bank.
 *                 0 means driver decides.
 * @port_pin_base: Pin index of the pin controller device.
 * @port_width: PIN number of the GPIO bank device
 * @pint_id: GPIO PINT device id that this GPIO bank should map to.
 * @pint_assign: The 32-bit GPIO PINT registers can be divided into 2 parts. A
 *               GPIO bank can be mapped into either low 16 bits[0] or high 16
 *               bits[1] of each PINT register.
 * @pint_map: GIOP bank mapping code in PINT device
 */
struct adi_pinctrl_gpio_platform_data {
	unsigned int port_gpio_base;
	unsigned int port_pin_base;
	unsigned int port_width;
	u8 pinctrl_id;
	u8 pint_id;
	bool pint_assign;
	u8 pint_map;
};

#endif
