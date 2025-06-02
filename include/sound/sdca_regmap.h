/* SPDX-License-Identifier: GPL-2.0 */
/*
 * The MIPI SDCA specification is available for public downloads at
 * https://www.mipi.org/mipi-sdca-v1-0-download
 *
 * Copyright (C) 2025 Cirrus Logic, Inc. and
 *                    Cirrus Logic International Semiconductor Ltd.
 */

#ifndef __SDCA_REGMAP_H__
#define __SDCA_REGMAP_H__

struct device;
struct sdca_function_data;
struct regmap;
struct reg_default;

bool sdca_regmap_readable(struct sdca_function_data *function, unsigned int reg);
bool sdca_regmap_writeable(struct sdca_function_data *function, unsigned int reg);
bool sdca_regmap_volatile(struct sdca_function_data *function, unsigned int reg);
bool sdca_regmap_deferrable(struct sdca_function_data *function, unsigned int reg);
int sdca_regmap_mbq_size(struct sdca_function_data *function, unsigned int reg);

int sdca_regmap_count_constants(struct device *dev, struct sdca_function_data *function);
int sdca_regmap_populate_constants(struct device *dev, struct sdca_function_data *function,
				   struct reg_default *consts);

int sdca_regmap_write_defaults(struct device *dev, struct regmap *regmap,
			       struct sdca_function_data *function);

#endif // __SDCA_REGMAP_H__
