/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 */

#ifndef _LINUX_QTI_REGMAP_DEBUGFS_H_
#define _LINUX_QTI_REGMAP_DEBUGFS_H_

#include <linux/device.h>
#include <linux/regmap.h>

#if IS_ENABLED(CONFIG_REGMAP_QTI_DEBUGFS)

int regmap_qti_debugfs_register(struct device *dev, struct regmap *regmap);
void regmap_qti_debugfs_unregister(struct regmap *regmap);
int devm_regmap_qti_debugfs_register(struct device *dev, struct regmap *regmap);
void devm_regmap_qti_debugfs_unregister(struct regmap *regmap);

#else

static inline int regmap_qti_debugfs_register(struct device *dev,
					      struct regmap *regmap)
{ return 0; }
static inline void regmap_qti_debugfs_unregister(struct regmap *regmap)
{ }
static inline int devm_regmap_qti_debugfs_register(struct device *dev,
						   struct regmap *regmap)
{ return 0; }
static inline void devm_regmap_qti_debugfs_unregister(struct regmap *regmap)
{ }

#endif
#endif
