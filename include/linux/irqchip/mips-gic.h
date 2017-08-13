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

/* GIC Address Space */
#define USM_VISIBLE_SECTION_OFS		0x10000
#define USM_VISIBLE_SECTION_SIZE	0x10000

/* User Mode Visible Section Register Map */
#define GIC_UMV_SH_COUNTER_31_00_OFS	0x0000
#define GIC_UMV_SH_COUNTER_63_32_OFS	0x0004

#ifdef CONFIG_MIPS_GIC

extern unsigned int gic_present;

extern void gic_init(unsigned long gic_base_addr,
	unsigned long gic_addrspace_size, unsigned int cpu_vec,
	unsigned int irqbase);
extern int gic_get_c0_compare_int(void);
extern int gic_get_c0_perfcount_int(void);
extern int gic_get_c0_fdc_int(void);
extern int gic_get_usm_range(struct resource *gic_usm_res);

#else /* CONFIG_MIPS_GIC */

#define gic_present	0

static inline int gic_get_usm_range(struct resource *gic_usm_res)
{
	/* Shouldn't be called. */
	return -1;
}

#endif /* CONFIG_MIPS_GIC */

#endif /* __LINUX_IRQCHIP_MIPS_GIC_H */
