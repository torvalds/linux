/*
 * Machine interface for the pinctrl subsystem.
 *
 * Copyright (C) 2011 ST-Ericsson SA
 * Written on behalf of Linaro for ST-Ericsson
 * Based on bits of regulator core, gpio core and clk core
 *
 * Author: Linus Walleij <linus.walleij@linaro.org>
 *
 * License terms: GNU General Public License (GPL) version 2
 */
#ifndef __LINUX_PINCTRL_MACHINE_H
#define __LINUX_PINCTRL_MACHINE_H

#include "pinctrl.h"

/**
 * struct pinctrl_map - boards/machines shall provide this map for devices
 * @dev_name: the name of the device using this specific mapping, the name
 *	must be the same as in your struct device*. If this name is set to the
 *	same name as the pin controllers own dev_name(), the map entry will be
 *	hogged by the driver itself upon registration
 * @name: the name of this specific map entry for the particular machine.
 *	This is the parameter passed to pinmux_lookup_state()
 * @ctrl_dev_name: the name of the device controlling this specific mapping,
 *	the name must be the same as in your struct device*
 * @group: sometimes a function can map to different pin groups, so this
 *	selects a certain specific pin group to activate for the function, if
 *	left as NULL, the first applicable group will be used
 * @function: a function in the driver to use for this mapping, the driver
 *	will lookup the function referenced by this ID on the specified
 *	pin control device
 */
struct pinctrl_map {
	const char *dev_name;
	const char *name;
	const char *ctrl_dev_name;
	const char *group;
	const char *function;
};

/*
 * Convenience macro to set a simple map from a certain pin controller and a
 * certain function to a named device
 */
#define PIN_MAP(a, b, c, d) \
	{ .name = a, .ctrl_dev_name = b, .function = c, .dev_name = d }

/*
 * Convenience macro to map a system function onto a certain pinctrl device,
 * to be hogged by the pin control core until the system shuts down.
 */
#define PIN_MAP_SYS_HOG(a, b) \
	{ .name = PINCTRL_STATE_DEFAULT, .ctrl_dev_name = a, .dev_name = a, \
	  .function = b, }

/*
 * Convenience macro to map a system function onto a certain pinctrl device
 * using a specified group, to be hogged by the pin control core until the
 * system shuts down.
 */
#define PIN_MAP_SYS_HOG_GROUP(a, b, c) \
	{ .name = PINCTRL_STATE_DEFAULT, .ctrl_dev_name = a, .dev_name = a, \
	  .function = b, .group = c, }

#ifdef CONFIG_PINMUX

extern int pinctrl_register_mappings(struct pinctrl_map const *map,
				unsigned num_maps);

#else

static inline int pinctrl_register_mappings(struct pinctrl_map const *map,
					   unsigned num_maps)
{
	return 0;
}

#endif /* !CONFIG_PINMUX */
#endif
