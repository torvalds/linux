/*
 * driver.h -- SoC Regulator driver support.
 *
 * Copyright (C) 2007, 2008 Wolfson Microelectronics PLC.
 *
 * Author: Liam Girdwood <lg@opensource.wolfsonmicro.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Regulator Driver Interface.
 */

#ifndef __LINUX_REGULATOR_DRIVER_H_
#define __LINUX_REGULATOR_DRIVER_H_

#include <linux/device.h>
#include <linux/regulator/consumer.h>

struct regulator_constraints;
struct regulator_dev;

/**
 * struct regulator_ops - regulator operations.
 *
 * This struct describes regulator operations.
 */
struct regulator_ops {

	/* get/set regulator voltage */
	int (*set_voltage) (struct regulator_dev *, int min_uV, int max_uV);
	int (*get_voltage) (struct regulator_dev *);

	/* get/set regulator current  */
	int (*set_current_limit) (struct regulator_dev *,
				 int min_uA, int max_uA);
	int (*get_current_limit) (struct regulator_dev *);

	/* enable/disable regulator */
	int (*enable) (struct regulator_dev *);
	int (*disable) (struct regulator_dev *);
	int (*is_enabled) (struct regulator_dev *);

	/* get/set regulator operating mode (defined in regulator.h) */
	int (*set_mode) (struct regulator_dev *, unsigned int mode);
	unsigned int (*get_mode) (struct regulator_dev *);

	/* get most efficient regulator operating mode for load */
	unsigned int (*get_optimum_mode) (struct regulator_dev *, int input_uV,
					  int output_uV, int load_uA);

	/* the operations below are for configuration of regulator state when
	 * it's parent PMIC enters a global STANBY/HIBERNATE state */

	/* set regulator suspend voltage */
	int (*set_suspend_voltage) (struct regulator_dev *, int uV);

	/* enable/disable regulator in suspend state */
	int (*set_suspend_enable) (struct regulator_dev *);
	int (*set_suspend_disable) (struct regulator_dev *);

	/* set regulator suspend operating mode (defined in regulator.h) */
	int (*set_suspend_mode) (struct regulator_dev *, unsigned int mode);
};

/*
 * Regulators can either control voltage or current.
 */
enum regulator_type {
	REGULATOR_VOLTAGE,
	REGULATOR_CURRENT,
};

/**
 * struct regulator_desc - Regulator descriptor
 *
 */
struct regulator_desc {
	const char *name;
	int id;
	struct regulator_ops *ops;
	int irq;
	enum regulator_type type;
	struct module *owner;
};


struct regulator_dev *regulator_register(struct regulator_desc *regulator_desc,
					  void *reg_data);
void regulator_unregister(struct regulator_dev *rdev);

int regulator_notifier_call_chain(struct regulator_dev *rdev,
				  unsigned long event, void *data);

void *rdev_get_drvdata(struct regulator_dev *rdev);
int rdev_get_id(struct regulator_dev *rdev);

#endif
