/* SPDX-License-Identifier: (GPL-2.0 OR MIT) */
/*
 * Driver for the MDIO interface of Microsemi network switches.
 *
 * Author: Colin Foster <colin.foster@in-advantage.com>
 * Copyright (C) 2021 Innovative Advantage
 */
#ifndef MDIO_MSCC_MIIM_H
#define MDIO_MSCC_MIIM_H

#include <linux/device.h>
#include <linux/phy.h>
#include <linux/regmap.h>

int mscc_miim_setup(struct device *device, struct mii_bus **bus,
		    const char *name, struct regmap *mii_regmap,
		    int status_offset, bool ignore_read_errors);

#endif
