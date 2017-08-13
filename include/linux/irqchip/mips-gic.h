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

#define GIC_MAX_INTRS			256

#define MSK(n) ((1 << (n)) - 1)

/* Accessors */
#define GIC_REG(segment, offset) (segment##_##SECTION_OFS + offset##_##OFS)

/* GIC Address Space */
#define VPE_LOCAL_SECTION_OFS		0x8000
#define VPE_LOCAL_SECTION_SIZE		0x4000
#define VPE_OTHER_SECTION_OFS		0xc000
#define VPE_OTHER_SECTION_SIZE		0x4000
#define USM_VISIBLE_SECTION_OFS		0x10000
#define USM_VISIBLE_SECTION_SIZE	0x10000

/* Register Map for Local Section */
#define GIC_VPE_CTL_OFS			0x0000
#define GIC_VPE_TIMER_MAP_OFS		0x0048
#define GIC_VPE_OTHER_ADDR_OFS		0x0080
#define GIC_VPE_WD_CONFIG0_OFS		0x0090
#define GIC_VPE_WD_COUNT0_OFS		0x0094
#define GIC_VPE_WD_INITIAL0_OFS		0x0098

#define GIC_VPE_EIC_SHADOW_SET_BASE_OFS	0x0100
#define GIC_VPE_EIC_SS(intr)		(4 * (intr))

#define GIC_VPE_EIC_VEC_BASE_OFS	0x0800
#define GIC_VPE_EIC_VEC(intr)		(4 * (intr))

#define GIC_VPE_TENABLE_NMI_OFS		0x1000
#define GIC_VPE_TENABLE_YQ_OFS		0x1004
#define GIC_VPE_TENABLE_INT_31_0_OFS	0x1080
#define GIC_VPE_TENABLE_INT_63_32_OFS	0x1084

/* User Mode Visible Section Register Map */
#define GIC_UMV_SH_COUNTER_31_00_OFS	0x0000
#define GIC_UMV_SH_COUNTER_63_32_OFS	0x0004

/* Masks */
#define GIC_MAP_SHF			0
#define GIC_MAP_MSK			(MSK(6) << GIC_MAP_SHF)

/* GIC_VPE_CTL Masks */
#define GIC_VPE_CTL_FDC_RTBL_SHF	4
#define GIC_VPE_CTL_FDC_RTBL_MSK	(MSK(1) << GIC_VPE_CTL_FDC_RTBL_SHF)
#define GIC_VPE_CTL_SWINT_RTBL_SHF	3
#define GIC_VPE_CTL_SWINT_RTBL_MSK	(MSK(1) << GIC_VPE_CTL_SWINT_RTBL_SHF)
#define GIC_VPE_CTL_PERFCNT_RTBL_SHF	2
#define GIC_VPE_CTL_PERFCNT_RTBL_MSK	(MSK(1) << GIC_VPE_CTL_PERFCNT_RTBL_SHF)
#define GIC_VPE_CTL_TIMER_RTBL_SHF	1
#define GIC_VPE_CTL_TIMER_RTBL_MSK	(MSK(1) << GIC_VPE_CTL_TIMER_RTBL_SHF)
#define GIC_VPE_CTL_EIC_MODE_SHF	0
#define GIC_VPE_CTL_EIC_MODE_MSK	(MSK(1) << GIC_VPE_CTL_EIC_MODE_SHF)

/* GIC nomenclature for Core Interrupt Pins. */
#define GIC_CPU_INT0		0 /* Core Interrupt 2 */
#define GIC_CPU_INT1		1 /* .		      */
#define GIC_CPU_INT2		2 /* .		      */
#define GIC_CPU_INT3		3 /* .		      */
#define GIC_CPU_INT4		4 /* .		      */
#define GIC_CPU_INT5		5 /* Core Interrupt 7 */

/* Add 2 to convert GIC CPU pin to core interrupt */
#define GIC_CPU_PIN_OFFSET	2

/* Add 2 to convert non-EIC hardware interrupt to EIC vector number. */
#define GIC_CPU_TO_VEC_OFFSET	2

/* Mapped interrupt to pin X, then GIC will generate the vector (X+1). */
#define GIC_PIN_TO_VEC_OFFSET	1

/* Local GIC interrupts. */
#define GIC_LOCAL_INT_WD	0 /* GIC watchdog */
#define GIC_LOCAL_INT_COMPARE	1 /* GIC count and compare timer */
#define GIC_LOCAL_INT_TIMER	2 /* CPU timer interrupt */
#define GIC_LOCAL_INT_PERFCTR	3 /* CPU performance counter */
#define GIC_LOCAL_INT_SWINT0	4 /* CPU software interrupt 0 */
#define GIC_LOCAL_INT_SWINT1	5 /* CPU software interrupt 1 */
#define GIC_LOCAL_INT_FDC	6 /* CPU fast debug channel */
#define GIC_NUM_LOCAL_INTRS	7

/* Convert between local/shared IRQ number and GIC HW IRQ number. */
#define GIC_LOCAL_HWIRQ_BASE	0
#define GIC_LOCAL_TO_HWIRQ(x)	(GIC_LOCAL_HWIRQ_BASE + (x))
#define GIC_HWIRQ_TO_LOCAL(x)	((x) - GIC_LOCAL_HWIRQ_BASE)
#define GIC_SHARED_HWIRQ_BASE	GIC_NUM_LOCAL_INTRS
#define GIC_SHARED_TO_HWIRQ(x)	(GIC_SHARED_HWIRQ_BASE + (x))
#define GIC_HWIRQ_TO_SHARED(x)	((x) - GIC_SHARED_HWIRQ_BASE)

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
