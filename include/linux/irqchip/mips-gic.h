/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2000, 07 MIPS Technologies, Inc.
 */
#ifndef __LINUX_IRQCHIP_MIPS_GIC_H
#define __LINUX_IRQCHIP_MIPS_GIC_H

#include <linux/clocksource.h>
#include <linux/ioport.h>

/* User Mode Visible Section Register Map */
#define GIC_UMV_SH_COUNTER_31_00_OFS	0x0000
#define GIC_UMV_SH_COUNTER_63_32_OFS	0x0004

#ifdef CONFIG_MIPS_GIC

extern int gic_get_c0_compare_int(void);
extern int gic_get_c0_perfcount_int(void);
extern int gic_get_c0_fdc_int(void);

#endif /* CONFIG_MIPS_GIC */

#endif /* __LINUX_IRQCHIP_MIPS_GIC_H */
