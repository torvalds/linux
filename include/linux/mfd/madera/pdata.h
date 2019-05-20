// SPDX-License-Identifier: GPL-2.0
/*
 * Platform data for Cirrus Logic Madera codecs
 *
 * Copyright (C) 2015-2018 Cirrus Logic
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by the
 * Free Software Foundation; version 2.
 */

#ifndef MADERA_PDATA_H
#define MADERA_PDATA_H

#include <linux/kernel.h>
#include <linux/regulator/arizona-ldo1.h>
#include <linux/regulator/arizona-micsupp.h>
#include <linux/regulator/machine.h>

#define MADERA_MAX_MICBIAS		4
#define MADERA_MAX_CHILD_MICBIAS	4

#define MADERA_MAX_GPSW			2

struct gpio_desc;
struct pinctrl_map;
struct madera_codec_pdata;

/**
 * struct madera_pdata - Configuration data for Madera devices
 *
 * @reset:	    GPIO controlling /RESET (NULL = none)
 * @ldo1:	    Substruct of pdata for the LDO1 regulator
 * @micvdd:	    Substruct of pdata for the MICVDD regulator
 * @irq_flags:	    Mode for primary IRQ (defaults to active low)
 * @gpio_base:	    Base GPIO number
 * @gpio_configs:   Array of GPIO configurations (See
 *		    Documentation/driver-api/pinctl.rst)
 * @n_gpio_configs: Number of entries in gpio_configs
 * @gpsw:	    General purpose switch mode setting. Depends on the external
 *		    hardware connected to the switch. (See the SW1_MODE field
 *		    in the datasheet for the available values for your codec)
 */
struct madera_pdata {
	struct gpio_desc *reset;

	struct arizona_ldo1_pdata ldo1;
	struct arizona_micsupp_pdata micvdd;

	unsigned int irq_flags;
	int gpio_base;

	const struct pinctrl_map *gpio_configs;
	int n_gpio_configs;

	u32 gpsw[MADERA_MAX_GPSW];
};

#endif
