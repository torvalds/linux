/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2004-2006 Atmel Corporation
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
