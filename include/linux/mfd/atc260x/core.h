/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Core MFD defines for ATC260x PMICs
 *
 * Copyright (C) 2019 Manivannan Sadhasivam <manivannan.sadhasivam@linaro.org>
 * Copyright (C) 2020 Cristian Ciocaltea <cristian.ciocaltea@gmail.com>
 */

#ifndef __LINUX_MFD_ATC260X_CORE_H
#define __LINUX_MFD_ATC260X_CORE_H

#include <linux/mfd/atc260x/atc2603c.h>
#include <linux/mfd/atc260x/atc2609a.h>

enum atc260x_type {
	ATC2603A = 0,
	ATC2603C,
	ATC2609A,
};

enum atc260x_ver {
	ATC260X_A = 0,
	ATC260X_B,
	ATC260X_C,
	ATC260X_D,
	ATC260X_E,
	ATC260X_F,
	ATC260X_G,
	ATC260X_H,
};

struct atc260x {
	struct device *dev;

	struct regmap *regmap;
	const struct regmap_irq_chip *regmap_irq_chip;
	struct regmap_irq_chip_data *irq_data;

	struct mutex *regmap_mutex;	/* mutex for custom regmap locking */

	const struct mfd_cell *cells;
	int nr_cells;
	int irq;

	enum atc260x_type ic_type;
	enum atc260x_ver ic_ver;
	const char *type_name;
	unsigned int rev_reg;

	const struct atc260x_init_regs *init_regs; /* regs for device init */
};

struct regmap_config;

int atc260x_match_device(struct atc260x *atc260x, struct regmap_config *regmap_cfg);
int atc260x_device_probe(struct atc260x *atc260x);

#endif /* __LINUX_MFD_ATC260X_CORE_H */
