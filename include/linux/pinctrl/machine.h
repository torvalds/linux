/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Machine interface for the pinctrl subsystem.
 *
 * Copyright (C) 2011 ST-Ericsson SA
 * Written on behalf of Linaro for ST-Ericsson
 * Based on bits of regulator core, gpio core and clk core
 *
 * Author: Linus Walleij <linus.walleij@linaro.org>
 */
#ifndef __LINUX_PINCTRL_MACHINE_H
#define __LINUX_PINCTRL_MACHINE_H

#include <linux/array_size.h>

#include <linux/pinctrl/pinctrl-state.h>

enum pinctrl_map_type {
	PIN_MAP_TYPE_INVALID,
	PIN_MAP_TYPE_DUMMY_STATE,
	PIN_MAP_TYPE_MUX_GROUP,
	PIN_MAP_TYPE_CONFIGS_PIN,
	PIN_MAP_TYPE_CONFIGS_GROUP,
};

/**
 * struct pinctrl_map_mux - mapping table content for MAP_TYPE_MUX_GROUP
 * @group: the name of the group whose mux function is to be configured. This
 *	field may be left NULL, and the first applicable group for the function
 *	will be used.
 * @function: the mux function to select for the group
 */
struct pinctrl_map_mux {
	const char *group;
	const char *function;
};

/**
 * struct pinctrl_map_configs - mapping table content for MAP_TYPE_CONFIGS_*
 * @group_or_pin: the name of the pin or group whose configuration parameters
 *	are to be configured.
 * @configs: a pointer to an array of config parameters/values to program into
 *	hardware. Each individual pin controller defines the format and meaning
 *	of config parameters.
 * @num_configs: the number of entries in array @configs
 */
struct pinctrl_map_configs {
	const char *group_or_pin;
	unsigned long *configs;
	unsigned int num_configs;
};

/**
 * struct pinctrl_map - boards/machines shall provide this map for devices
 * @dev_name: the name of the device using this specific mapping, the name
 *	must be the same as in your struct device*. If this name is set to the
 *	same name as the pin controllers own dev_name(), the map entry will be
 *	hogged by the driver itself upon registration
 * @name: the name of this specific map entry for the particular machine.
 *	This is the parameter passed to pinmux_lookup_state()
 * @type: the type of mapping table entry
 * @ctrl_dev_name: the name of the device controlling this specific mapping,
 *	the name must be the same as in your struct device*. This field is not
 *	used for PIN_MAP_TYPE_DUMMY_STATE
 * @data: Data specific to the mapping type
 */
struct pinctrl_map {
	const char *dev_name;
	const char *name;
	enum pinctrl_map_type type;
	const char *ctrl_dev_name;
	union {
		struct pinctrl_map_mux mux;
		struct pinctrl_map_configs configs;
	} data;
};

/* Convenience macros to create mapping table entries */

#define PIN_MAP_DUMMY_STATE(dev, state) \
	{								\
		.dev_name = dev,					\
		.name = state,						\
		.type = PIN_MAP_TYPE_DUMMY_STATE,			\
	}

#define PIN_MAP_MUX_GROUP(dev, state, pinctrl, grp, func)		\
	{								\
		.dev_name = dev,					\
		.name = state,						\
		.type = PIN_MAP_TYPE_MUX_GROUP,				\
		.ctrl_dev_name = pinctrl,				\
		.data.mux = {						\
			.group = grp,					\
			.function = func,				\
		},							\
	}

#define PIN_MAP_MUX_GROUP_DEFAULT(dev, pinctrl, grp, func)		\
	PIN_MAP_MUX_GROUP(dev, PINCTRL_STATE_DEFAULT, pinctrl, grp, func)

#define PIN_MAP_MUX_GROUP_HOG(dev, state, grp, func)			\
	PIN_MAP_MUX_GROUP(dev, state, dev, grp, func)

#define PIN_MAP_MUX_GROUP_HOG_DEFAULT(dev, grp, func)			\
	PIN_MAP_MUX_GROUP(dev, PINCTRL_STATE_DEFAULT, dev, grp, func)

#define PIN_MAP_CONFIGS_PIN(dev, state, pinctrl, pin, cfgs)		\
	{								\
		.dev_name = dev,					\
		.name = state,						\
		.type = PIN_MAP_TYPE_CONFIGS_PIN,			\
		.ctrl_dev_name = pinctrl,				\
		.data.configs = {					\
			.group_or_pin = pin,				\
			.configs = cfgs,				\
			.num_configs = ARRAY_SIZE(cfgs),		\
		},							\
	}

#define PIN_MAP_CONFIGS_PIN_DEFAULT(dev, pinctrl, pin, cfgs)		\
	PIN_MAP_CONFIGS_PIN(dev, PINCTRL_STATE_DEFAULT, pinctrl, pin, cfgs)

#define PIN_MAP_CONFIGS_PIN_HOG(dev, state, pin, cfgs)			\
	PIN_MAP_CONFIGS_PIN(dev, state, dev, pin, cfgs)

#define PIN_MAP_CONFIGS_PIN_HOG_DEFAULT(dev, pin, cfgs)			\
	PIN_MAP_CONFIGS_PIN(dev, PINCTRL_STATE_DEFAULT, dev, pin, cfgs)

#define PIN_MAP_CONFIGS_GROUP(dev, state, pinctrl, grp, cfgs)		\
	{								\
		.dev_name = dev,					\
		.name = state,						\
		.type = PIN_MAP_TYPE_CONFIGS_GROUP,			\
		.ctrl_dev_name = pinctrl,				\
		.data.configs = {					\
			.group_or_pin = grp,				\
			.configs = cfgs,				\
			.num_configs = ARRAY_SIZE(cfgs),		\
		},							\
	}

#define PIN_MAP_CONFIGS_GROUP_DEFAULT(dev, pinctrl, grp, cfgs)		\
	PIN_MAP_CONFIGS_GROUP(dev, PINCTRL_STATE_DEFAULT, pinctrl, grp, cfgs)

#define PIN_MAP_CONFIGS_GROUP_HOG(dev, state, grp, cfgs)		\
	PIN_MAP_CONFIGS_GROUP(dev, state, dev, grp, cfgs)

#define PIN_MAP_CONFIGS_GROUP_HOG_DEFAULT(dev, grp, cfgs)		\
	PIN_MAP_CONFIGS_GROUP(dev, PINCTRL_STATE_DEFAULT, dev, grp, cfgs)

struct pinctrl_map;

#ifdef CONFIG_PINCTRL

extern int pinctrl_register_mappings(const struct pinctrl_map *map,
				     unsigned int num_maps);
extern void pinctrl_unregister_mappings(const struct pinctrl_map *map);
extern void pinctrl_provide_dummies(void);
#else

static inline int pinctrl_register_mappings(const struct pinctrl_map *map,
					    unsigned int num_maps)
{
	return 0;
}

static inline void pinctrl_unregister_mappings(const struct pinctrl_map *map)
{
}

static inline void pinctrl_provide_dummies(void)
{
}
#endif /* !CONFIG_PINCTRL */
#endif
