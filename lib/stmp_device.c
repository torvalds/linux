/*
 * Copyright (C) 1999 ARM Limited
 * Copyright (C) 2000 Deep Blue Solutions Ltd
 * Copyright 2006-2007,2010 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright 2008 Juergen Beisert, kernel@pengutronix.de
 * Copyright 2009 Ilya Yanok, Emcraft Systems Ltd, yanok@emcraft.com
 * Copyright (C) 2011 Wolfram Sang, Pengutronix e.K.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/io.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/stmp_device.h>

#define STMP_MODULE_CLKGATE	(1 << 30)
#define STMP_MODULE_SFTRST	(1 << 31)

/*
 * Clear the bit and poll it cleared.  This is usually called with
 * a reset address and mask being either SFTRST(bit 31) or CLKGATE
 * (bit 30).
 */
static int stmp_clear_poll_bit(void __iomem *addr, u32 mask)
{
	int timeout = 0x400;

	writel(mask, addr + STMP_OFFSET_REG_CLR);
	udelay(1);
	while ((readl(addr) & mask) && --timeout)
		/* nothing */;

	return !timeout;
}

int stmp_reset_block(void __iomem *reset_addr)
{
	int ret;
	int timeout = 0x400;

	/* clear and poll SFTRST */
	ret = stmp_clear_poll_bit(reset_addr, STMP_MODULE_SFTRST);
	if (unlikely(ret))
		goto error;

	/* clear CLKGATE */
	writel(STMP_MODULE_CLKGATE, reset_addr + STMP_OFFSET_REG_CLR);

	/* set SFTRST to reset the block */
	writel(STMP_MODULE_SFTRST, reset_addr + STMP_OFFSET_REG_SET);
	udelay(1);

	/* poll CLKGATE becoming set */
	while ((!(readl(reset_addr) & STMP_MODULE_CLKGATE)) && --timeout)
		/* nothing */;
	if (unlikely(!timeout))
		goto error;

	/* clear and poll SFTRST */
	ret = stmp_clear_poll_bit(reset_addr, STMP_MODULE_SFTRST);
	if (unlikely(ret))
		goto error;

	/* clear and poll CLKGATE */
	ret = stmp_clear_poll_bit(reset_addr, STMP_MODULE_CLKGATE);
	if (unlikely(ret))
		goto error;

	return 0;

error:
	pr_err("%s(%p): module reset timeout\n", __func__, reset_addr);
	return -ETIMEDOUT;
}
EXPORT_SYMBOL(stmp_reset_block);
