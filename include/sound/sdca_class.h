/* SPDX-License-Identifier: GPL-2.0 */
/*
 * The MIPI SDCA specification is available for public downloads at
 * https://www.mipi.org/mipi-sdca-v1-0-download
 *
 * Copyright (C) 2025 Cirrus Logic, Inc. and
 *                    Cirrus Logic International Semiconductor Ltd.
 */

#ifndef __SDCA_CLASS_H__
#define __SDCA_CLASS_H__

#include <linux/completion.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>

struct device;
struct regmap;
struct sdw_slave;
struct sdca_function_data;

/**
 * struct sdca_class_ops - optional codec-provided class callbacks
 * @populate_function: fill @function (entities, clusters, init_table, ...)
 *           from static tables in place of sdca_parse_function() on
 *           DT/non-DisCo platforms.  Called once per SDCA function with
 *           @dev pointing at that function's device.  Pure data source;
 *           performs no bus I/O.  Return 0 on success or a negative errno.
 */
struct sdca_class_ops {
	int (*populate_function)(struct device *dev,
				 struct sdca_function_data *function);
};

struct sdca_class_drv {
	struct device *dev;
	struct regmap *dev_regmap;
	struct sdw_slave *sdw;

	struct sdca_interrupt_info *irq_info;

	const struct sdca_class_ops *ops;

	struct mutex regmap_lock;
	/* Serialise function initialisations */
	struct mutex init_lock;
	struct work_struct boot_work;
};

/* Library helpers used by codec-specific SDCA SoundWire drivers. */
int sdca_class_probe(struct sdw_slave *sdw,
		     struct sdca_class_drv *drv,
		     const struct sdca_class_ops *ops);
void sdca_class_remove(struct sdca_class_drv *drv);

/*
 * PM helpers.  Codec drivers embed sdca_class_drv in their own priv,
 * own dev_set_drvdata(), and compose these into their own dev_pm_ops:
 *
 *	static int wcd_runtime_suspend(struct device *dev) {
 *		struct wcd_priv *priv = dev_get_drvdata(dev);
 *		return sdca_class_runtime_suspend(&priv->class);
 *	}
 */
int sdca_class_runtime_suspend(struct sdca_class_drv *drv);
int sdca_class_runtime_resume(struct sdca_class_drv *drv);
int sdca_class_system_suspend(struct sdca_class_drv *drv);
int sdca_class_system_resume(struct sdca_class_drv *drv);

#endif /* __SDCA_CLASS_H__ */
