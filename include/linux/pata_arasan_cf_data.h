/*
 * include/linux/pata_arasan_cf_data.h
 *
 * Arasan Compact Flash host controller platform data header file
 *
 * Copyright (C) 2011 ST Microelectronics
 * Viresh Kumar <viresh.linux@gmail.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#ifndef _PATA_ARASAN_CF_DATA_H
#define _PATA_ARASAN_CF_DATA_H

#include <linux/platform_device.h>

struct arasan_cf_pdata {
	u8 cf_if_clk;
	#define CF_IF_CLK_100M			(0x0)
	#define CF_IF_CLK_75M			(0x1)
	#define CF_IF_CLK_66M			(0x2)
	#define CF_IF_CLK_50M			(0x3)
	#define CF_IF_CLK_40M			(0x4)
	#define CF_IF_CLK_33M			(0x5)
	#define CF_IF_CLK_25M			(0x6)
	#define CF_IF_CLK_125M			(0x7)
	#define CF_IF_CLK_150M			(0x8)
	#define CF_IF_CLK_166M			(0x9)
	#define CF_IF_CLK_200M			(0xA)
	/*
	 * Platform specific incapabilities of CF controller is handled via
	 * quirks
	 */
	u32 quirk;
	#define CF_BROKEN_PIO			(1)
	#define CF_BROKEN_MWDMA			(1 << 1)
	#define CF_BROKEN_UDMA			(1 << 2)
};

static inline void
set_arasan_cf_pdata(struct platform_device *pdev, struct arasan_cf_pdata *data)
{
	pdev->dev.platform_data = data;
}
#endif /* _PATA_ARASAN_CF_DATA_H */
