/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Definitions for the Linux I2C OF component prober
 *
 * Copyright (C) 2024 Google LLC
 */

#ifndef _LINUX_I2C_OF_PROBER_H
#define _LINUX_I2C_OF_PROBER_H

#include <linux/kconfig.h>
#include <linux/types.h>

struct device;
struct device_node;

/**
 * struct i2c_of_probe_ops - I2C OF component prober callbacks
 *
 * A set of callbacks to be used by i2c_of_probe_component().
 *
 * All callbacks are optional. Callbacks are called only once per run, and are
 * used in the order they are defined in this structure.
 *
 * All callbacks that have return values shall return %0 on success,
 * or a negative error number on failure.
 *
 * The @dev parameter passed to the callbacks is the same as @dev passed to
 * i2c_of_probe_component(). It should only be used for dev_printk() calls
 * and nothing else, especially not managed device resource (devres) APIs.
 */
struct i2c_of_probe_ops {
	/**
	 * @enable: Retrieve and enable resources so that the components respond to probes.
	 *
	 * It is OK for this callback to return -EPROBE_DEFER since the intended use includes
	 * retrieving resources and enables them. Resources should be reverted to their initial
	 * state and released before returning if this fails.
	 */
	int (*enable)(struct device *dev, struct device_node *bus_node, void *data);

	/**
	 * @cleanup_early: Release exclusive resources prior to calling probe() on a
	 *		   detected component.
	 *
	 * Only called if a matching component is actually found. If none are found,
	 * resources that would have been released in this callback should be released in
	 * @free_resourcs_late instead.
	 */
	void (*cleanup_early)(struct device *dev, void *data);

	/**
	 * @cleanup: Opposite of @enable to balance refcounts and free resources after probing.
	 *
	 * Should check if resources were already freed by @cleanup_early.
	 */
	void (*cleanup)(struct device *dev, void *data);
};

/**
 * struct i2c_of_probe_cfg - I2C OF component prober configuration
 * @ops: Callbacks for the prober to use.
 * @type: A string to match the device node name prefix to probe for.
 */
struct i2c_of_probe_cfg {
	const struct i2c_of_probe_ops *ops;
	const char *type;
};

#if IS_ENABLED(CONFIG_OF_DYNAMIC)

int i2c_of_probe_component(struct device *dev, const struct i2c_of_probe_cfg *cfg, void *ctx);

/**
 * DOC: I2C OF component prober simple helpers
 *
 * Components such as trackpads are commonly connected to a devices baseboard
 * with a 6-pin ribbon cable. That gives at most one voltage supply and one
 * GPIO (commonly a "enable" or "reset" line) besides the I2C bus, interrupt
 * pin, and common ground. Touchscreens, while integrated into the display
 * panel's connection, typically have the same set of connections.
 *
 * A simple set of helpers are provided here for use with the I2C OF component
 * prober. This implementation targets such components, allowing for at most
 * one regulator supply.
 *
 * The following helpers are provided:
 * * i2c_of_probe_simple_enable()
 * * i2c_of_probe_simple_cleanup_early()
 * * i2c_of_probe_simple_cleanup()
 */

/**
 * struct i2c_of_probe_simple_opts - Options for simple I2C component prober callbacks
 * @res_node_compatible: Compatible string of device node to retrieve resources from.
 * @supply_name: Name of regulator supply.
 * @gpio_name: Name of GPIO. NULL if no GPIO line is used. Empty string ("") if GPIO
 *	       line is unnamed.
 * @post_power_on_delay_ms: Delay after regulators are powered on. Passed to msleep().
 * @post_gpio_config_delay_ms: Delay after GPIO is configured. Passed to msleep().
 * @gpio_assert_to_enable: %true if GPIO should be asserted, i.e. set to logical high,
 *			   to enable the component.
 *
 * This describes power sequences common for the class of components supported by the
 * simple component prober:
 * * @gpio_name is configured to the non-active setting according to @gpio_assert_to_enable.
 * * @supply_name regulator supply is enabled.
 * * Wait for @post_power_on_delay_ms to pass.
 * * @gpio_name is configured to the active setting according to @gpio_assert_to_enable.
 * * Wait for @post_gpio_config_delay_ms to pass.
 */
struct i2c_of_probe_simple_opts {
	const char *res_node_compatible;
	const char *supply_name;
	const char *gpio_name;
	unsigned int post_power_on_delay_ms;
	unsigned int post_gpio_config_delay_ms;
	bool gpio_assert_to_enable;
};

struct gpio_desc;
struct regulator;

struct i2c_of_probe_simple_ctx {
	/* public: provided by user before helpers are used. */
	const struct i2c_of_probe_simple_opts *opts;
	/* private: internal fields for helpers. */
	struct regulator *supply;
	struct gpio_desc *gpiod;
};

int i2c_of_probe_simple_enable(struct device *dev, struct device_node *bus_node, void *data);
void i2c_of_probe_simple_cleanup_early(struct device *dev, void *data);
void i2c_of_probe_simple_cleanup(struct device *dev, void *data);

extern struct i2c_of_probe_ops i2c_of_probe_simple_ops;

#endif /* IS_ENABLED(CONFIG_OF_DYNAMIC) */

#endif /* _LINUX_I2C_OF_PROBER_H */
