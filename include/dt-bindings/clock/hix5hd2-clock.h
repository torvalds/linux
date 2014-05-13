/*
 * Copyright (c) 2014 Linaro Ltd.
 * Copyright (c) 2014 Hisilicon Limited.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 */

#ifndef __DTS_HIX5HD2_CLOCK_H
#define __DTS_HIX5HD2_CLOCK_H

/* fixed rate */
#define HIX5HD2_FIXED_1200M		1
#define HIX5HD2_FIXED_400M		2
#define HIX5HD2_FIXED_48M		3
#define HIX5HD2_FIXED_24M		4
#define HIX5HD2_FIXED_600M		5
#define HIX5HD2_FIXED_300M		6
#define HIX5HD2_FIXED_75M		7
#define HIX5HD2_FIXED_200M		8
#define HIX5HD2_FIXED_100M		9
#define HIX5HD2_FIXED_40M		10
#define HIX5HD2_FIXED_150M		11
#define HIX5HD2_FIXED_1728M		12
#define HIX5HD2_FIXED_28P8M		13
#define HIX5HD2_FIXED_432M		14
#define HIX5HD2_FIXED_345P6M		15
#define HIX5HD2_FIXED_288M		16
#define HIX5HD2_FIXED_60M		17
#define HIX5HD2_FIXED_750M		18
#define HIX5HD2_FIXED_500M		19
#define HIX5HD2_FIXED_54M		20
#define HIX5HD2_FIXED_27M		21
#define HIX5HD2_FIXED_1500M		22
#define HIX5HD2_FIXED_375M		23
#define HIX5HD2_FIXED_187M		24
#define HIX5HD2_FIXED_250M		25
#define HIX5HD2_FIXED_125M		26
#define HIX5HD2_FIXED_2P02M		27
#define HIX5HD2_FIXED_50M		28
#define HIX5HD2_FIXED_25M		29
#define HIX5HD2_FIXED_83M		30

/* mux clocks */
#define HIX5HD2_SFC_MUX			64
#define HIX5HD2_MMC_MUX			65
#define HIX5HD2_FEPHY_MUX		66

/* gate clocks */
#define HIX5HD2_SFC_RST			128
#define HIX5HD2_SFC_CLK			129
#define HIX5HD2_MMC_CIU_CLK		130
#define HIX5HD2_MMC_BIU_CLK		131
#define HIX5HD2_MMC_CIU_RST		132
#define HIX5HD2_FWD_BUS_CLK		133
#define HIX5HD2_FWD_SYS_CLK		134
#define HIX5HD2_MAC0_PHY_CLK		135

/* complex */
#define HIX5HD2_MAC0_CLK		192
#define HIX5HD2_MAC1_CLK		193
#define HIX5HD2_SATA_CLK		194
#define HIX5HD2_USB_CLK			195

#define HIX5HD2_NR_CLKS			256
#endif	/* __DTS_HIX5HD2_CLOCK_H */
