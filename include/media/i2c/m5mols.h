/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Driver header for M-5MOLS 8M Pixel camera sensor with ISP
 *
 * Copyright (C) 2011 Samsung Electronics Co., Ltd.
 * Author: HeungJun Kim <riverful.kim@samsung.com>
 *
 * Copyright (C) 2009 Samsung Electronics Co., Ltd.
 * Author: Dongsoo Nathaniel Kim <dongsoo45.kim@samsung.com>
 */

#ifndef MEDIA_M5MOLS_H
#define MEDIA_M5MOLS_H

/**
 * struct m5mols_platform_data - platform data for M-5MOLS driver
 * @set_power:	an additional callback to the board setup code
 *		to be called after enabling and before disabling
 *		the sensor's supply regulators
 */
struct m5mols_platform_data {
	int (*set_power)(struct device *dev, int on);
};

#endif	/* MEDIA_M5MOLS_H */
