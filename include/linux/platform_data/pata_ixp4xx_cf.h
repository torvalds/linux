/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PLATFORM_DATA_PATA_IXP4XX_H
#define __PLATFORM_DATA_PATA_IXP4XX_H

#include <linux/types.h>

/*
 * This structure provide a means for the board setup code
 * to give information to th pata_ixp4xx driver. It is
 * passed as platform_data.
 */
struct ixp4xx_pata_data {
	volatile u32	*cs0_cfg;
	volatile u32	*cs1_cfg;
	unsigned long	cs0_bits;
	unsigned long	cs1_bits;
	void __iomem	*cs0;
	void __iomem	*cs1;
};

#endif
