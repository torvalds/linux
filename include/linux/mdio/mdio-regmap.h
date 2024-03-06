/* SPDX-License-Identifier: GPL-2.0 */
/* Driver for MMIO-Mapped MDIO devices. Some IPs expose internal PHYs or PCS
 * within the MMIO-mapped area
 *
 * Copyright (C) 2023 Maxime Chevallier <maxime.chevallier@bootlin.com>
 */
#ifndef MDIO_REGMAP_H
#define MDIO_REGMAP_H

#include <linux/phy.h>

struct device;
struct regmap;

struct mdio_regmap_config {
	struct device *parent;
	struct regmap *regmap;
	char name[MII_BUS_ID_SIZE];
	u8 valid_addr;
	bool autoscan;
};

struct mii_bus *devm_mdio_regmap_register(struct device *dev,
					  const struct mdio_regmap_config *config);

#endif
