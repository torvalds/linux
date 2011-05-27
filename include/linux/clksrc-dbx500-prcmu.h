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

extern void __iomem *clksrc_dbx500_timer_base;

#ifdef CONFIG_CLKSRC_DBX500_PRCMU
void __init clksrc_dbx500_prcmu_init(void);
#else
void __init clksrc_dbx500_prcmu_init(void) {}
#endif

#endif
