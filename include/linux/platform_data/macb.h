/*
 * Copyright (C) 2004-2006 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __MACB_PDATA_H__
#define __MACB_PDATA_H__

#include <linux/clk.h>

/**
 * struct macb_platform_data - platform data for MACB Ethernet
 * @pclk:		platform clock
 * @hclk:		AHB clock
 */
struct macb_platform_data {
	struct clk	*pclk;
	struct clk	*hclk;
};

#endif /* __MACB_PDATA_H__ */
