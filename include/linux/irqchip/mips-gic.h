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

/* Constants */
#define GIC_POL_POS			1
#define GIC_POL_NEG			0
#define GIC_TRIG_EDGE			1
#define GIC_TRIG_LEVEL			0
#define GIC_TRIG_DUAL_ENABLE		1
#define GIC_TRIG_DUAL_DISABLE		0

#define MSK(n) ((1 << (n)) - 1)

/* Accessors */
#define GIC_REG(segment, offset) (segment##_##SECTION_OFS + offset##_##OFS)

/* GIC Address Space */
#define SHARED_SECTION_OFS		0x0000
#define SHARED_SECTION_SIZE		0x8000
#define VPE_LOCAL_SECTION_OFS		0x8000
#define VPE_LOCAL_SECTION_SIZE		0x4000
#define VPE_OTHER_SECTION_OFS		0xc000
#define VPE_OTHER_SECTION_SIZE		0x4000
#define USM_VISIBLE_SECTION_OFS		0x10000
#define USM_VISIBLE_SECTION_SIZE	0x10000

/* Register Map for Shared Section */

#define GIC_SH_CONFIG_OFS		0x0000

/* Shared Global Counter */
#define GIC_SH_COUNTER_31_00_OFS	0x0010
/* 64-bit counter register for CM3 */
#define GIC_SH_COUNTER_OFS		GIC_SH_COUNTER_31_00_OFS
#define GIC_SH_COUNTER_63_32_OFS	0x0014
#define GIC_SH_REVISIONID_OFS		0x0020

/* Convert an interrupt number to a byte offset/bit for multi-word registers */
#define GIC_INTR_OFS(intr) ({				\
	unsigned bits = mips_cm_is64 ? 64 : 32;		\
	unsigned reg_idx = (intr) / bits;		\
	unsigned reg_width = bits / 8;			\
							\
	reg_idx * reg_width;				\
})
#define GIC_INTR_BIT(intr)		((intr) % (mips_cm_is64 ? 64 : 32))

/* Polarity : Reset Value is always 0 */
#define GIC_SH_SET_POLARITY_OFS		0x0100

/* Triggering : Reset Value is always 0 */
#define GIC_SH_SET_TRIGGER_OFS		0x0180

/* Dual edge triggering : Reset Value is always 0 */
#define GIC_SH_SET_DUAL_OFS		0x0200

/* Set/Clear corresponding bit in Edge Detect Register */
#define GIC_SH_WEDGE_OFS		0x0280

/* Mask manipulation */
#define GIC_SH_RMASK_OFS		0x0300
#define GIC_SH_SMASK_OFS		0x0380

/* Global Interrupt Mask Register (RO) - Bit Set == Interrupt enabled */
#define GIC_SH_MASK_OFS			0x0400

/* Pending Global Interrupts (RO) */
#define GIC_SH_PEND_OFS			0x0480

/* Maps Interrupt X to a Pin */
#define GIC_SH_INTR_MAP_TO_PIN_BASE_OFS 0x0500
#define GIC_SH_MAP_TO_PIN(intr)		(4 * (intr))

/* Maps Interrupt X to a VPE */
#define GIC_SH_INTR_MAP_TO_VPE_BASE_OFS 0x2000
#define GIC_SH_MAP_TO_VPE_REG_OFF(intr, vpe) \
	((32 * (intr)) + (((vpe) / 32) * 4))
#define GIC_SH_MAP_TO_VPE_REG_BIT(vpe)	(1 << ((vpe) % 32))

/* Register Map for Local Section */
#define GIC_VPE_CTL_OFS			0x0000
#define GIC_VPE_PEND_OFS		0x0004
#define GIC_VPE_MASK_OFS		0x0008
#define GIC_VPE_RMASK_OFS		0x000c
#define GIC_VPE_SMASK_OFS		0x0010
#define GIC_VPE_WD_MAP_OFS		0x0040
#define GIC_VPE_COMPARE_MAP_OFS		0x0044
#define GIC_VPE_TIMER_MAP_OFS		0x0048
#define GIC_VPE_FDC_MAP_OFS		0x004c
#define GIC_VPE_PERFCTR_MAP_OFS		0x0050
#define GIC_VPE_SWINT0_MAP_OFS		0x0054
#define GIC_VPE_SWINT1_MAP_OFS		0x0058
#define GIC_VPE_OTHER_ADDR_OFS		0x0080
#define GIC_VPE_WD_CONFIG0_OFS		0x0090
#define GIC_VPE_WD_COUNT0_OFS		0x0094
#define GIC_VPE_WD_INITIAL0_OFS		0x0098
#define GIC_VPE_COMPARE_LO_OFS		0x00a0
/* 64-bit Compare register on CM3 */
#define GIC_VPE_COMPARE_OFS		GIC_VPE_COMPARE_LO_OFS
#define GIC_VPE_COMPARE_HI_OFS		0x00a4

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
#define GIC_SH_CONFIG_COUNTSTOP_SHF	28
#define GIC_SH_CONFIG_COUNTSTOP_MSK	(MSK(1) << GIC_SH_CONFIG_COUNTSTOP_SHF)

#define GIC_SH_CONFIG_COUNTBITS_SHF	24
#define GIC_SH_CONFIG_COUNTBITS_MSK	(MSK(4) << GIC_SH_CONFIG_COUNTBITS_SHF)

#define GIC_SH_CONFIG_NUMINTRS_SHF	16
#define GIC_SH_CONFIG_NUMINTRS_MSK	(MSK(8) << GIC_SH_CONFIG_NUMINTRS_SHF)

#define GIC_SH_CONFIG_NUMVPES_SHF	0
#define GIC_SH_CONFIG_NUMVPES_MSK	(MSK(8) << GIC_SH_CONFIG_NUMVPES_SHF)

#define GIC_SH_WEDGE_SET(intr)		((intr) | (0x1 << 31))
#define GIC_SH_WEDGE_CLR(intr)		((intr) & ~(0x1 << 31))

#define GIC_MAP_TO_PIN_SHF		31
#define GIC_MAP_TO_PIN_MSK		(MSK(1) << GIC_MAP_TO_PIN_SHF)
#define GIC_MAP_TO_NMI_SHF		30
#define GIC_MAP_TO_NMI_MSK		(MSK(1) << GIC_MAP_TO_NMI_SHF)
#define GIC_MAP_TO_YQ_SHF		29
#define GIC_MAP_TO_YQ_MSK		(MSK(1) << GIC_MAP_TO_YQ_SHF)
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

/* GIC_VPE_PEND Masks */
#define GIC_VPE_PEND_WD_SHF		0
#define GIC_VPE_PEND_WD_MSK		(MSK(1) << GIC_VPE_PEND_WD_SHF)
#define GIC_VPE_PEND_CMP_SHF		1
#define GIC_VPE_PEND_CMP_MSK		(MSK(1) << GIC_VPE_PEND_CMP_SHF)
#define GIC_VPE_PEND_TIMER_SHF		2
#define GIC_VPE_PEND_TIMER_MSK		(MSK(1) << GIC_VPE_PEND_TIMER_SHF)
#define GIC_VPE_PEND_PERFCOUNT_SHF	3
#define GIC_VPE_PEND_PERFCOUNT_MSK	(MSK(1) << GIC_VPE_PEND_PERFCOUNT_SHF)
#define GIC_VPE_PEND_SWINT0_SHF		4
#define GIC_VPE_PEND_SWINT0_MSK		(MSK(1) << GIC_VPE_PEND_SWINT0_SHF)
#define GIC_VPE_PEND_SWINT1_SHF		5
#define GIC_VPE_PEND_SWINT1_MSK		(MSK(1) << GIC_VPE_PEND_SWINT1_SHF)
#define GIC_VPE_PEND_FDC_SHF		6
#define GIC_VPE_PEND_FDC_MSK		(MSK(1) << GIC_VPE_PEND_FDC_SHF)

/* GIC_VPE_RMASK Masks */
#define GIC_VPE_RMASK_WD_SHF		0
#define GIC_VPE_RMASK_WD_MSK		(MSK(1) << GIC_VPE_RMASK_WD_SHF)
#define GIC_VPE_RMASK_CMP_SHF		1
#define GIC_VPE_RMASK_CMP_MSK		(MSK(1) << GIC_VPE_RMASK_CMP_SHF)
#define GIC_VPE_RMASK_TIMER_SHF		2
#define GIC_VPE_RMASK_TIMER_MSK		(MSK(1) << GIC_VPE_RMASK_TIMER_SHF)
#define GIC_VPE_RMASK_PERFCNT_SHF	3
#define GIC_VPE_RMASK_PERFCNT_MSK	(MSK(1) << GIC_VPE_RMASK_PERFCNT_SHF)
#define GIC_VPE_RMASK_SWINT0_SHF	4
#define GIC_VPE_RMASK_SWINT0_MSK	(MSK(1) << GIC_VPE_RMASK_SWINT0_SHF)
#define GIC_VPE_RMASK_SWINT1_SHF	5
#define GIC_VPE_RMASK_SWINT1_MSK	(MSK(1) << GIC_VPE_RMASK_SWINT1_SHF)
#define GIC_VPE_RMASK_FDC_SHF		6
#define GIC_VPE_RMASK_FDC_MSK		(MSK(1) << GIC_VPE_RMASK_FDC_SHF)

/* GIC_VPE_SMASK Masks */
#define GIC_VPE_SMASK_WD_SHF		0
#define GIC_VPE_SMASK_WD_MSK		(MSK(1) << GIC_VPE_SMASK_WD_SHF)
#define GIC_VPE_SMASK_CMP_SHF		1
#define GIC_VPE_SMASK_CMP_MSK		(MSK(1) << GIC_VPE_SMASK_CMP_SHF)
#define GIC_VPE_SMASK_TIMER_SHF		2
#define GIC_VPE_SMASK_TIMER_MSK		(MSK(1) << GIC_VPE_SMASK_TIMER_SHF)
#define GIC_VPE_SMASK_PERFCNT_SHF	3
#define GIC_VPE_SMASK_PERFCNT_MSK	(MSK(1) << GIC_VPE_SMASK_PERFCNT_SHF)
#define GIC_VPE_SMASK_SWINT0_SHF	4
#define GIC_VPE_SMASK_SWINT0_MSK	(MSK(1) << GIC_VPE_SMASK_SWINT0_SHF)
#define GIC_VPE_SMASK_SWINT1_SHF	5
#define GIC_VPE_SMASK_SWINT1_MSK	(MSK(1) << GIC_VPE_SMASK_SWINT1_SHF)
#define GIC_VPE_SMASK_FDC_SHF		6
#define GIC_VPE_SMASK_FDC_MSK		(MSK(1) << GIC_VPE_SMASK_FDC_SHF)

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
extern void gic_clocksource_init(unsigned int);
extern cycle_t gic_read_count(void);
extern unsigned int gic_get_count_width(void);
extern cycle_t gic_read_compare(void);
extern void gic_write_compare(cycle_t cnt);
extern void gic_write_cpu_compare(cycle_t cnt, int cpu);
extern void gic_start_count(void);
extern void gic_stop_count(void);
extern void gic_send_ipi(unsigned int intr);
extern unsigned int plat_ipi_call_int_xlate(unsigned int);
extern unsigned int plat_ipi_resched_int_xlate(unsigned int);
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
