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
#ifndef __LINUX_PINMUX_MACHINE_H
#define __LINUX_PINMUX_MACHINE_H

/**
 * struct pinmux_map - boards/machines shall provide this map for devices
 * @name: the name of this specific map entry for the particular machine.
 *	This is the second parameter passed to pinmux_get() when you want
 *	to have several mappings to the same device
 * @ctrl_dev: the pin control device to be used by this mapping, may be NULL
 *	if you provide .ctrl_dev_name instead (this is more common)
 * @ctrl_dev_name: the name of the device controlling this specific mapping,
 *	the name must be the same as in your struct device*, may be NULL if
 *	you provide .ctrl_dev instead
 * @function: a function in the driver to use for this mapping, the driver
 *	will lookup the function referenced by this ID on the specified
 *	pin control device
 * @group: sometimes a function can map to different pin groups, so this
 *	selects a certain specific pin group to activate for the function, if
 *	left as NULL, the first applicable group will be used
 * @dev: the device using this specific mapping, may be NULL if you provide
 *	.dev_name instead (this is more common)
 * @dev_name: the name of the device using this specific mapping, the name
 *	must be the same as in your struct device*, may be NULL if you
 *	provide .dev instead
 * @hog_on_boot: if this is set to true, the pin control subsystem will itself
 *	hog the mappings as the pinmux device drivers are attached, so this is
 *	typically used with system maps (mux mappings without an assigned
 *	device) that you want to get hogged and enabled by default as soon as
 *	a pinmux device supporting it is registered. These maps will not be
 *	disabled and put until the system shuts down.
 */
struct pinmux_map {
	const char *name;
	struct device *ctrl_dev;
	const char *ctrl_dev_name;
	const char *function;
	const char *group;
	struct device *dev;
	const char *dev_name;
	bool hog_on_boot;
};

/*
 * Convenience macro to set a simple map from a certain pin controller and a
 * certain function to a named device
 */
#define PINMUX_MAP(a, b, c, d) \
	{ .name = a, .ctrl_dev_name = b, .function = c, .dev_name = d }

/*
 * Convenience macro to map a system function onto a certain pinctrl device.
 * System functions are not assigned to a particular device.
 */
#define PINMUX_MAP_SYS(a, b, c) \
	{ .name = a, .ctrl_dev_name = b, .function = c }

/*
 * Convenience macro to map a system function onto a certain pinctrl device,
 * to be hogged by the pinmux core until the system shuts down.
 */
#define PINMUX_MAP_SYS_HOG(a, b, c) \
	{ .name = a, .ctrl_dev_name = b, .function = c, \
	  .hog_on_boot = true }

/*
 * Convenience macro to map a system function onto a certain pinctrl device
 * using a specified group, to be hogged by the pinmux core until the system
 * shuts down.
 */
#define PINMUX_MAP_SYS_HOG_GROUP(a, b, c, d)		\
	{ .name = a, .ctrl_dev_name = b, .function = c, .group = d, \
	  .hog_on_boot = true }

#ifdef CONFIG_PINMUX

extern int pinmux_register_mappings(struct pinmux_map const *map,
				unsigned num_maps);

#else

static inline int pinmux_register_mappings(struct pinmux_map const *map,
					   unsigned num_maps)
{
	return 0;
}

#endif /* !CONFIG_PINMUX */
#endif
