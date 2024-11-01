/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * TI DaVinci AEMIF platform glue.
 *
 * Copyright (C) 2017 BayLibre SAS
 *
 * Author:
 *   Bartosz Golaszewski <bgolaszewski@baylibre.com>
 */

#ifndef __TI_DAVINCI_AEMIF_DATA_H__
#define __TI_DAVINCI_AEMIF_DATA_H__

#include <linux/of_platform.h>

/**
 * struct aemif_abus_data - Async bus configuration parameters.
 *
 * @cs - Chip-select number.
 */
struct aemif_abus_data {
	u32 cs;
};

/**
 * struct aemif_platform_data - Data to set up the TI aemif driver.
 *
 * @dev_lookup: of_dev_auxdata passed to of_platform_populate() for aemif
 *              subdevices.
 * @cs_offset: Lowest allowed chip-select number.
 * @abus_data: Array of async bus configuration entries.
 * @num_abus_data: Number of abus entries.
 * @sub_devices: Array of platform subdevices.
 * @num_sub_devices: Number of subdevices.
 */
struct aemif_platform_data {
	struct of_dev_auxdata *dev_lookup;
	u32 cs_offset;
	struct aemif_abus_data *abus_data;
	size_t num_abus_data;
	struct platform_device *sub_devices;
	size_t num_sub_devices;
};

#endif /* __TI_DAVINCI_AEMIF_DATA_H__ */
