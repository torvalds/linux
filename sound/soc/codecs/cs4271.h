/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _CS4271_PRIV_H
#define _CS4271_PRIV_H

#include <linux/regmap.h>

extern const struct of_device_id cs4271_dt_ids[];
extern const struct regmap_config cs4271_regmap_config;

int cs4271_probe(struct device *dev, struct regmap *regmap);

#endif
