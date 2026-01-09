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

struct sdca_class_drv {
	struct device *dev;
	struct regmap *dev_regmap;
	struct sdw_slave *sdw;

	struct sdca_function_data *functions;
	struct sdca_interrupt_info *irq_info;

	struct mutex regmap_lock;
	/* Serialise function initialisations */
	struct mutex init_lock;
	struct work_struct boot_work;
	struct completion device_attach;

	bool attached;
};

#endif /* __SDCA_CLASS_H__ */
