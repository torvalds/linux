/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Author: Kevin Wells <kevin.wells@nxp.com>
 *
 * Copyright (C) 2010 NXP Semiconductors
 */

#ifndef __SOC_LPC32XX_MISC_H
#define __SOC_LPC32XX_MISC_H

#include <linux/types.h>
#include <linux/phy.h>

#ifdef CONFIG_ARCH_LPC32XX
extern u32 lpc32xx_return_iram(void __iomem **mapbase, dma_addr_t *dmaaddr);
extern void lpc32xx_set_phy_interface_mode(phy_interface_t mode);
extern void lpc32xx_loopback_set(resource_size_t mapbase, int state);
#else
static inline u32 lpc32xx_return_iram(void __iomem **mapbase, dma_addr_t *dmaaddr)
{
	*mapbase = NULL;
	*dmaaddr = 0;
	return 0;
}
static inline void lpc32xx_set_phy_interface_mode(phy_interface_t mode)
{
}
static inline void lpc32xx_loopback_set(resource_size_t mapbase, int state)
{
}
#endif

#endif  /* __SOC_LPC32XX_MISC_H */
