/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Author: Kevin Wells <kevin.wells@nxp.com>
 *
 * Copyright (C) 2010 NXP Semiconductors
 */

#ifndef __SOC_LPC32XX_MISC_H
#define __SOC_LPC32XX_MISC_H

#include <linux/types.h>

#ifdef CONFIG_ARCH_LPC32XX
extern u32 lpc32xx_return_iram(void __iomem **mapbase, dma_addr_t *dmaaddr);
#else
static inline u32 lpc32xx_return_iram(void __iomem **mapbase, dma_addr_t *dmaaddr)
{
	*mapbase = NULL;
	*dmaaddr = 0;
	return 0;
}
#endif

#endif  /* __SOC_LPC32XX_MISC_H */
