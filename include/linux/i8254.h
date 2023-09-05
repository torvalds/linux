/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) William Breathitt Gray */
#ifndef _I8254_H_
#define _I8254_H_

struct device;
struct regmap;

/**
 * struct i8254_regmap_config - Configuration for the register map of an i8254
 * @parent:	parent device
 * @map:	regmap for the i8254
 */
struct i8254_regmap_config {
	struct device *parent;
	struct regmap *map;
};

int devm_i8254_regmap_register(struct device *dev, const struct i8254_regmap_config *config);

#endif /* _I8254_H_ */
