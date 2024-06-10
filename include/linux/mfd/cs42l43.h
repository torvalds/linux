/* SPDX-License-Identifier: GPL-2.0 */
/*
 * CS42L43 core driver external data
 *
 * Copyright (C) 2022-2023 Cirrus Logic, Inc. and
 *                         Cirrus Logic International Semiconductor Ltd.
 */

#ifndef CS42L43_CORE_EXT_H
#define CS42L43_CORE_EXT_H

#include <linux/completion.h>
#include <linux/mutex.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/workqueue.h>

#define CS42L43_N_SUPPLIES		3

struct device;
struct gpio_desc;
struct sdw_slave;

enum cs42l43_irq_numbers {
	CS42L43_PLL_LOST_LOCK,
	CS42L43_PLL_READY,

	CS42L43_HP_STARTUP_DONE,
	CS42L43_HP_SHUTDOWN_DONE,
	CS42L43_HSDET_DONE,
	CS42L43_TIPSENSE_UNPLUG_DB,
	CS42L43_TIPSENSE_PLUG_DB,
	CS42L43_RINGSENSE_UNPLUG_DB,
	CS42L43_RINGSENSE_PLUG_DB,
	CS42L43_TIPSENSE_UNPLUG_PDET,
	CS42L43_TIPSENSE_PLUG_PDET,
	CS42L43_RINGSENSE_UNPLUG_PDET,
	CS42L43_RINGSENSE_PLUG_PDET,

	CS42L43_HS2_BIAS_SENSE,
	CS42L43_HS1_BIAS_SENSE,
	CS42L43_DC_DETECT1_FALSE,
	CS42L43_DC_DETECT1_TRUE,
	CS42L43_HSBIAS_CLAMPED,
	CS42L43_HS3_4_BIAS_SENSE,

	CS42L43_AMP2_CLK_STOP_FAULT,
	CS42L43_AMP1_CLK_STOP_FAULT,
	CS42L43_AMP2_VDDSPK_FAULT,
	CS42L43_AMP1_VDDSPK_FAULT,
	CS42L43_AMP2_SHUTDOWN_DONE,
	CS42L43_AMP1_SHUTDOWN_DONE,
	CS42L43_AMP2_STARTUP_DONE,
	CS42L43_AMP1_STARTUP_DONE,
	CS42L43_AMP2_THERM_SHDN,
	CS42L43_AMP1_THERM_SHDN,
	CS42L43_AMP2_THERM_WARN,
	CS42L43_AMP1_THERM_WARN,
	CS42L43_AMP2_SCDET,
	CS42L43_AMP1_SCDET,

	CS42L43_GPIO3_FALL,
	CS42L43_GPIO3_RISE,
	CS42L43_GPIO2_FALL,
	CS42L43_GPIO2_RISE,
	CS42L43_GPIO1_FALL,
	CS42L43_GPIO1_RISE,

	CS42L43_HP_ILIMIT,
	CS42L43_HP_LOADDET_DONE,
};

struct cs42l43 {
	struct device *dev;
	struct regmap *regmap;
	struct sdw_slave *sdw;

	struct regulator *vdd_p;
	struct regulator *vdd_d;
	struct regulator_bulk_data core_supplies[CS42L43_N_SUPPLIES];

	struct gpio_desc *reset;

	int irq;
	struct regmap_irq_chip irq_chip;
	struct regmap_irq_chip_data *irq_data;

	struct work_struct boot_work;
	struct completion device_attach;
	struct completion device_detach;
	struct completion firmware_download;
	int firmware_error;

	unsigned int sdw_freq;
	/* Lock to gate control of the PLL and its sources. */
	struct mutex pll_lock;

	bool sdw_pll_active;
	bool attached;
	bool hw_lock;
};

#endif /* CS42L43_CORE_EXT_H */
