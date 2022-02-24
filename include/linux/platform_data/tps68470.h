/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * TI TPS68470 PMIC platform data definition.
 *
 * Copyright (c) 2021 Red Hat Inc.
 *
 * Red Hat authors:
 * Hans de Goede <hdegoede@redhat.com>
 */
#ifndef __PDATA_TPS68470_H
#define __PDATA_TPS68470_H

enum tps68470_regulators {
	TPS68470_CORE,
	TPS68470_ANA,
	TPS68470_VCM,
	TPS68470_VIO,
	TPS68470_VSIO,
	TPS68470_AUX1,
	TPS68470_AUX2,
	TPS68470_NUM_REGULATORS
};

struct regulator_init_data;

struct tps68470_regulator_platform_data {
	const struct regulator_init_data *reg_init_data[TPS68470_NUM_REGULATORS];
};

struct tps68470_clk_platform_data {
	const char *consumer_dev_name;
	const char *consumer_con_id;
};

#endif
