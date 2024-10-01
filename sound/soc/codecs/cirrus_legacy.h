/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Some small helpers for older Cirrus Logic parts.
 *
 * Copyright (C) 2021 Cirrus Logic, Inc. and
 *                    Cirrus Logic International Semiconductor Ltd.
 */

static inline int cirrus_read_device_id(struct regmap *regmap, unsigned int reg)
{
	u8 devid[3];
	int ret;

	ret = regmap_bulk_read(regmap, reg, devid, ARRAY_SIZE(devid));
	if (ret < 0)
		return ret;

	return ((devid[0] & 0xFF) << 12) |
	       ((devid[1] & 0xFF) <<  4) |
	       ((devid[2] & 0xF0) >>  4);
}
