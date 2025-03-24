/* SPDX-License-Identifier: (GPL-2.0+ OR BSD-3-Clause) */
/* Copyright 2024 NXP
 */
#ifndef __NETC_GLOBAL_H
#define __NETC_GLOBAL_H

#include <linux/io.h>

static inline u32 netc_read(void __iomem *reg)
{
	return ioread32(reg);
}

static inline void netc_write(void __iomem *reg, u32 val)
{
	iowrite32(val, reg);
}

#endif
