/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Broadcom BCM63xx Processor Monitor Bus shared routines (SMP and reset)
 *
 * Copyright (C) 2015, Broadcom Corporation
 * Author: Florian Fainelli <f.fainelli@gmail.com>
 */
#ifndef __BCM63XX_PMB_H
#define __BCM63XX_PMB_H

#include <linux/io.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <linux/err.h>

/* PMB Master controller register */
#define PMB_CTRL		0x00
#define  PMC_PMBM_START		(1 << 31)
#define  PMC_PMBM_TIMEOUT	(1 << 30)
#define  PMC_PMBM_SLAVE_ERR	(1 << 29)
#define  PMC_PMBM_BUSY		(1 << 28)
#define  PMC_PMBM_READ		(0 << 20)
#define  PMC_PMBM_WRITE		(1 << 20)
#define PMB_WR_DATA		0x04
#define PMB_TIMEOUT		0x08
#define PMB_RD_DATA		0x0C

#define PMB_BUS_ID_SHIFT	8

/* Perform the low-level PMB master operation, shared between reads and
 * writes.
 */
static inline int __bpcm_do_op(void __iomem *master, unsigned int addr,
			       u32 off, u32 op)
{
	unsigned int timeout = 1000;
	u32 cmd;

	cmd = (PMC_PMBM_START | op | (addr & 0xff) << 12 | off);
	writel(cmd, master + PMB_CTRL);
	do {
		cmd = readl(master + PMB_CTRL);
		if (!(cmd & PMC_PMBM_START))
			return 0;

		if (cmd & PMC_PMBM_SLAVE_ERR)
			return -EIO;

		if (cmd & PMC_PMBM_TIMEOUT)
			return -ETIMEDOUT;

		udelay(1);
	} while (timeout-- > 0);

	return -ETIMEDOUT;
}

static inline int bpcm_rd(void __iomem *master, unsigned int addr,
			  u32 off, u32 *val)
{
	int ret = 0;

	ret = __bpcm_do_op(master, addr, off >> 2, PMC_PMBM_READ);
	*val = readl(master + PMB_RD_DATA);

	return ret;
}

static inline int bpcm_wr(void __iomem *master, unsigned int addr,
			  u32 off, u32 val)
{
	int ret = 0;

	writel(val, master + PMB_WR_DATA);
	ret = __bpcm_do_op(master, addr, off >> 2, PMC_PMBM_WRITE);

	return ret;
}

#endif /* __BCM63XX_PMB_H */
