/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ARM Generic Interrupt Controller (GIC) v3 specific defines
 */

#ifndef SELFTEST_KVM_GICV3_H
#define SELFTEST_KVM_GICV3_H

#include <asm/sysreg.h>

/*
 * Distributor registers
 */
#define GICD_CTLR			0x0000
#define GICD_TYPER			0x0004
#define GICD_IGROUPR			0x0080
#define GICD_ISENABLER			0x0100
#define GICD_ICENABLER			0x0180
#define GICD_ICACTIVER			0x0380
#define GICD_IPRIORITYR			0x0400

/*
 * The assumption is that the guest runs in a non-secure mode.
 * The following bits of GICD_CTLR are defined accordingly.
 */
#define GICD_CTLR_RWP			(1U << 31)
#define GICD_CTLR_nASSGIreq		(1U << 8)
#define GICD_CTLR_ARE_NS		(1U << 4)
#define GICD_CTLR_ENABLE_G1A		(1U << 1)
#define GICD_CTLR_ENABLE_G1		(1U << 0)

#define GICD_TYPER_SPIS(typer)		((((typer) & 0x1f) + 1) * 32)
#define GICD_INT_DEF_PRI_X4		0xa0a0a0a0

/*
 * Redistributor registers
 */
#define GICR_CTLR			0x000
#define GICR_WAKER			0x014

#define GICR_CTLR_RWP			(1U << 3)

#define GICR_WAKER_ProcessorSleep	(1U << 1)
#define GICR_WAKER_ChildrenAsleep	(1U << 2)

/*
 * Redistributor registers, offsets from SGI base
 */
#define GICR_IGROUPR0			GICD_IGROUPR
#define GICR_ISENABLER0			GICD_ISENABLER
#define GICR_ICENABLER0			GICD_ICENABLER
#define GICR_ICACTIVER0			GICD_ICACTIVER
#define GICR_IPRIORITYR0		GICD_IPRIORITYR

/* CPU interface registers */
#define SYS_ICC_PMR_EL1			sys_reg(3, 0, 4, 6, 0)
#define SYS_ICC_IAR1_EL1		sys_reg(3, 0, 12, 12, 0)
#define SYS_ICC_EOIR1_EL1		sys_reg(3, 0, 12, 12, 1)
#define SYS_ICC_SRE_EL1			sys_reg(3, 0, 12, 12, 5)
#define SYS_ICC_GRPEN1_EL1		sys_reg(3, 0, 12, 12, 7)

#define ICC_PMR_DEF_PRIO		0xf0

#define ICC_SRE_EL1_SRE			(1U << 0)

#define ICC_IGRPEN1_EL1_ENABLE		(1U << 0)

#define GICV3_MAX_CPUS			512

#endif /* SELFTEST_KVM_GICV3_H */
