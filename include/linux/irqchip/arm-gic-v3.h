/*
 * Copyright (C) 2013, 2014 ARM Limited, All Rights Reserved.
 * Author: Marc Zyngier <marc.zyngier@arm.com>
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef __LINUX_IRQCHIP_ARM_GIC_V3_H
#define __LINUX_IRQCHIP_ARM_GIC_V3_H

#include <asm/sysreg.h>

/*
 * Distributor registers. We assume we're running non-secure, with ARE
 * being set. Secure-only and non-ARE registers are not described.
 */
#define GICD_CTLR			0x0000
#define GICD_TYPER			0x0004
#define GICD_IIDR			0x0008
#define GICD_STATUSR			0x0010
#define GICD_SETSPI_NSR			0x0040
#define GICD_CLRSPI_NSR			0x0048
#define GICD_SETSPI_SR			0x0050
#define GICD_CLRSPI_SR			0x0058
#define GICD_SEIR			0x0068
#define GICD_ISENABLER			0x0100
#define GICD_ICENABLER			0x0180
#define GICD_ISPENDR			0x0200
#define GICD_ICPENDR			0x0280
#define GICD_ISACTIVER			0x0300
#define GICD_ICACTIVER			0x0380
#define GICD_IPRIORITYR			0x0400
#define GICD_ICFGR			0x0C00
#define GICD_IROUTER			0x6000
#define GICD_PIDR2			0xFFE8

#define GICD_CTLR_RWP			(1U << 31)
#define GICD_CTLR_ARE_NS		(1U << 4)
#define GICD_CTLR_ENABLE_G1A		(1U << 1)
#define GICD_CTLR_ENABLE_G1		(1U << 0)

#define GICD_IROUTER_SPI_MODE_ONE	(0U << 31)
#define GICD_IROUTER_SPI_MODE_ANY	(1U << 31)

#define GIC_PIDR2_ARCH_MASK		0xf0
#define GIC_PIDR2_ARCH_GICv3		0x30
#define GIC_PIDR2_ARCH_GICv4		0x40

/*
 * Re-Distributor registers, offsets from RD_base
 */
#define GICR_CTLR			GICD_CTLR
#define GICR_IIDR			0x0004
#define GICR_TYPER			0x0008
#define GICR_STATUSR			GICD_STATUSR
#define GICR_WAKER			0x0014
#define GICR_SETLPIR			0x0040
#define GICR_CLRLPIR			0x0048
#define GICR_SEIR			GICD_SEIR
#define GICR_PROPBASER			0x0070
#define GICR_PENDBASER			0x0078
#define GICR_INVLPIR			0x00A0
#define GICR_INVALLR			0x00B0
#define GICR_SYNCR			0x00C0
#define GICR_MOVLPIR			0x0100
#define GICR_MOVALLR			0x0110
#define GICR_PIDR2			GICD_PIDR2

#define GICR_WAKER_ProcessorSleep	(1U << 1)
#define GICR_WAKER_ChildrenAsleep	(1U << 2)

/*
 * Re-Distributor registers, offsets from SGI_base
 */
#define GICR_ISENABLER0			GICD_ISENABLER
#define GICR_ICENABLER0			GICD_ICENABLER
#define GICR_ISPENDR0			GICD_ISPENDR
#define GICR_ICPENDR0			GICD_ICPENDR
#define GICR_ISACTIVER0			GICD_ISACTIVER
#define GICR_ICACTIVER0			GICD_ICACTIVER
#define GICR_IPRIORITYR0		GICD_IPRIORITYR
#define GICR_ICFGR0			GICD_ICFGR

#define GICR_TYPER_VLPIS		(1U << 1)
#define GICR_TYPER_LAST			(1U << 4)

/*
 * CPU interface registers
 */
#define ICC_CTLR_EL1_EOImode_drop_dir	(0U << 1)
#define ICC_CTLR_EL1_EOImode_drop	(1U << 1)
#define ICC_SRE_EL1_SRE			(1U << 0)

/*
 * Hypervisor interface registers (SRE only)
 */
#define ICH_LR_VIRTUAL_ID_MASK		((1UL << 32) - 1)

#define ICH_LR_EOI			(1UL << 41)
#define ICH_LR_GROUP			(1UL << 60)
#define ICH_LR_STATE			(3UL << 62)
#define ICH_LR_PENDING_BIT		(1UL << 62)
#define ICH_LR_ACTIVE_BIT		(1UL << 63)

#define ICH_MISR_EOI			(1 << 0)
#define ICH_MISR_U			(1 << 1)

#define ICH_HCR_EN			(1 << 0)
#define ICH_HCR_UIE			(1 << 1)

#define ICH_VMCR_CTLR_SHIFT		0
#define ICH_VMCR_CTLR_MASK		(0x21f << ICH_VMCR_CTLR_SHIFT)
#define ICH_VMCR_BPR1_SHIFT		18
#define ICH_VMCR_BPR1_MASK		(7 << ICH_VMCR_BPR1_SHIFT)
#define ICH_VMCR_BPR0_SHIFT		21
#define ICH_VMCR_BPR0_MASK		(7 << ICH_VMCR_BPR0_SHIFT)
#define ICH_VMCR_PMR_SHIFT		24
#define ICH_VMCR_PMR_MASK		(0xffUL << ICH_VMCR_PMR_SHIFT)

#define ICC_EOIR1_EL1			sys_reg(3, 0, 12, 12, 1)
#define ICC_IAR1_EL1			sys_reg(3, 0, 12, 12, 0)
#define ICC_SGI1R_EL1			sys_reg(3, 0, 12, 11, 5)
#define ICC_PMR_EL1			sys_reg(3, 0, 4, 6, 0)
#define ICC_CTLR_EL1			sys_reg(3, 0, 12, 12, 4)
#define ICC_SRE_EL1			sys_reg(3, 0, 12, 12, 5)
#define ICC_GRPEN1_EL1			sys_reg(3, 0, 12, 12, 7)

#define ICC_IAR1_EL1_SPURIOUS		0x3ff

#define ICC_SRE_EL2			sys_reg(3, 4, 12, 9, 5)

#define ICC_SRE_EL2_SRE			(1 << 0)
#define ICC_SRE_EL2_ENABLE		(1 << 3)

/*
 * System register definitions
 */
#define ICH_VSEIR_EL2			sys_reg(3, 4, 12, 9, 4)
#define ICH_HCR_EL2			sys_reg(3, 4, 12, 11, 0)
#define ICH_VTR_EL2			sys_reg(3, 4, 12, 11, 1)
#define ICH_MISR_EL2			sys_reg(3, 4, 12, 11, 2)
#define ICH_EISR_EL2			sys_reg(3, 4, 12, 11, 3)
#define ICH_ELSR_EL2			sys_reg(3, 4, 12, 11, 5)
#define ICH_VMCR_EL2			sys_reg(3, 4, 12, 11, 7)

#define __LR0_EL2(x)			sys_reg(3, 4, 12, 12, x)
#define __LR8_EL2(x)			sys_reg(3, 4, 12, 13, x)

#define ICH_LR0_EL2			__LR0_EL2(0)
#define ICH_LR1_EL2			__LR0_EL2(1)
#define ICH_LR2_EL2			__LR0_EL2(2)
#define ICH_LR3_EL2			__LR0_EL2(3)
#define ICH_LR4_EL2			__LR0_EL2(4)
#define ICH_LR5_EL2			__LR0_EL2(5)
#define ICH_LR6_EL2			__LR0_EL2(6)
#define ICH_LR7_EL2			__LR0_EL2(7)
#define ICH_LR8_EL2			__LR8_EL2(0)
#define ICH_LR9_EL2			__LR8_EL2(1)
#define ICH_LR10_EL2			__LR8_EL2(2)
#define ICH_LR11_EL2			__LR8_EL2(3)
#define ICH_LR12_EL2			__LR8_EL2(4)
#define ICH_LR13_EL2			__LR8_EL2(5)
#define ICH_LR14_EL2			__LR8_EL2(6)
#define ICH_LR15_EL2			__LR8_EL2(7)

#define __AP0Rx_EL2(x)			sys_reg(3, 4, 12, 8, x)
#define ICH_AP0R0_EL2			__AP0Rx_EL2(0)
#define ICH_AP0R1_EL2			__AP0Rx_EL2(1)
#define ICH_AP0R2_EL2			__AP0Rx_EL2(2)
#define ICH_AP0R3_EL2			__AP0Rx_EL2(3)

#define __AP1Rx_EL2(x)			sys_reg(3, 4, 12, 9, x)
#define ICH_AP1R0_EL2			__AP1Rx_EL2(0)
#define ICH_AP1R1_EL2			__AP1Rx_EL2(1)
#define ICH_AP1R2_EL2			__AP1Rx_EL2(2)
#define ICH_AP1R3_EL2			__AP1Rx_EL2(3)

#ifndef __ASSEMBLY__

#include <linux/stringify.h>

static inline void gic_write_eoir(u64 irq)
{
	asm volatile("msr_s " __stringify(ICC_EOIR1_EL1) ", %0" : : "r" (irq));
	isb();
}

#endif

#endif
