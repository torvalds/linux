/*
 * TI DaVinci AEMIF platform glue.
 *
 * Copyright (C) 2017 BayLibre SAS
 *
 * Author:
 *   Bartosz Golaszewski <bgolaszewski@baylibre.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __TI_DAVINCI_AEMIF_DATA_H__
#define __TI_DAVINCI_AEMIF_DATA_H__

#include <linux/of_platform.h>

struct aemif_platform_data {
	struct of_dev_auxdata *dev_lookup;
};

#endif /* __TI_DAVINCI_AEMIF_DATA_H__ */
