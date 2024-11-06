/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Definitions for the Linux I2C OF component prober
 *
 * Copyright (C) 2024 Google LLC
 */

#ifndef _LINUX_I2C_OF_PROBER_H
#define _LINUX_I2C_OF_PROBER_H

#include <linux/kconfig.h>

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

#endif /* IS_ENABLED(CONFIG_OF_DYNAMIC) */

#endif /* _LINUX_I2C_OF_PROBER_H */
