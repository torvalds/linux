/*
 * Copyright (C) ST-Ericsson SA 2011
 *
 * License Terms: GNU General Public License v2
 * Author: Mattias Wallin <mattias.wallin@stericsson.com>
 *
 */
#ifndef __CLKSRC_DBX500_PRCMU_H
#define __CLKSRC_DBX500_PRCMU_H

#include <linux/init.h>
#include <linux/io.h>

#ifdef CONFIG_CLKSRC_DBX500_PRCMU
void __init clksrc_dbx500_prcmu_init(void __iomem *base);
#else
static inline void __init clksrc_dbx500_prcmu_init(void __iomem *base) {}
#endif

#endif
