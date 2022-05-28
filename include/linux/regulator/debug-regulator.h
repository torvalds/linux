/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 */

#ifndef _LINUX_REGULATOR_DEBUG_CONTROL_H_
#define _LINUX_REGULATOR_DEBUG_CONTROL_H_

struct device;
struct regulator_dev;

#if IS_ENABLED(CONFIG_REGULATOR_DEBUG_CONTROL)

int regulator_debug_register(struct device *dev, struct regulator_dev *rdev);
void regulator_debug_unregister(struct regulator_dev *rdev);
int devm_regulator_debug_register(struct device *dev,
				  struct regulator_dev *rdev);
void devm_regulator_debug_unregister(struct regulator_dev *rdev);

#else

static inline int regulator_debug_register(struct device *dev,
					   struct regulator_dev *rdev)
{ return 0; }
static inline void regulator_debug_unregister(struct regulator_dev *rdev)
{ }
static inline int devm_regulator_debug_register(struct device *dev,
						struct regulator_dev *rdev)
{ return 0; }
static inline void devm_regulator_debug_unregister(struct regulator_dev *rdev)
{ }

#endif
#endif
