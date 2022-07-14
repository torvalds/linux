/* SPDX-License-Identifier: GPL-2.0 */
/* mt9t112 Camera
 *
 * Copyright (C) 2009 Renesas Solutions Corp.
 * Kuninori Morimoto <morimoto.kuninori@renesas.com>
 */

#ifndef __MT9T112_H__
#define __MT9T112_H__

struct mt9t112_pll_divider {
	u8 m, n;
	u8 p1, p2, p3, p4, p5, p6, p7;
};

/**
 * struct mt9t112_platform_data - mt9t112 driver interface
 * @flags:			Sensor media bus configuration.
 * @divider:			Sensor PLL configuration
 */
struct mt9t112_platform_data {
#define MT9T112_FLAG_PCLK_RISING_EDGE	BIT(0)
	u32 flags;
	struct mt9t112_pll_divider divider;
};

#endif /* __MT9T112_H__ */
