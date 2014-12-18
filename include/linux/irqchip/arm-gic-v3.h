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

#define GICD_TYPER_ID_BITS(typer)	((((typer) >> 19) & 0x1f) + 1)
#define GICD_TYPER_IRQS(typer)		((((typer) & 0x1f) + 1) * 32)
#define GICD_TYPER_LPIS			(1U << 17)

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

#define GICR_CTLR_ENABLE_LPIS		(1UL << 0)

#define GICR_TYPER_CPU_NUMBER(r)	(((r) >> 8) & 0xffff)

#define GICR_WAKER_ProcessorSleep	(1U << 1)
#define GICR_WAKER_ChildrenAsleep	(1U << 2)

#define GICR_PROPBASER_NonShareable	(0U << 10)
#define GICR_PROPBASER_InnerShareable	(1U << 10)
#define GICR_PROPBASER_OuterShareable	(2U << 10)
#define GICR_PROPBASER_SHAREABILITY_MASK (3UL << 10)
#define GICR_PROPBASER_nCnB		(0U << 7)
#define GICR_PROPBASER_nC		(1U << 7)
#define GICR_PROPBASER_RaWt		(2U << 7)
#define GICR_PROPBASER_RaWb		(3U << 7)
#define GICR_PROPBASER_WaWt		(4U << 7)
#define GICR_PROPBASER_WaWb		(5U << 7)
#define GICR_PROPBASER_RaWaWt		(6U << 7)
#define GICR_PROPBASER_RaWaWb		(7U << 7)
#define GICR_PROPBASER_IDBITS_MASK	(0x1f)

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

#define GICR_TYPER_PLPIS		(1U << 0)
#define GICR_TYPER_VLPIS		(1U << 1)
#define GICR_TYPER_LAST			(1U << 4)

#define LPI_PROP_GROUP1			(1 << 1)
#define LPI_PROP_ENABLED		(1 << 0)

/*
 * ITS registers, offsets from ITS_base
 */
#define GITS_CTLR			0x0000
#define GITS_IIDR			0x0004
#define GITS_TYPER			0x0008
#define GITS_CBASER			0x0080
#define GITS_CWRITER			0x0088
#define GITS_CREADR			0x0090
#define GITS_BASER			0x0100
#define GITS_PIDR2			GICR_PIDR2

#define GITS_TRANSLATER			0x10040

#define GITS_TYPER_PTA			(1UL << 19)

#define GITS_CBASER_VALID		(1UL << 63)
#define GITS_CBASER_nCnB		(0UL << 59)
#define GITS_CBASER_nC			(1UL << 59)
#define GITS_CBASER_RaWt		(2UL << 59)
#define GITS_CBASER_RaWb		(3UL << 59)
#define GITS_CBASER_WaWt		(4UL << 59)
#define GITS_CBASER_WaWb		(5UL << 59)
#define GITS_CBASER_RaWaWt		(6UL << 59)
#define GITS_CBASER_RaWaWb		(7UL << 59)
#define GITS_CBASER_NonShareable	(0UL << 10)
#define GITS_CBASER_InnerShareable	(1UL << 10)
#define GITS_CBASER_OuterShareable	(2UL << 10)
#define GITS_CBASER_SHAREABILITY_MASK	(3UL << 10)

#define GITS_BASER_NR_REGS		8

#define GITS_BASER_VALID		(1UL << 63)
#define GITS_BASER_nCnB			(0UL << 59)
#define GITS_BASER_nC			(1UL << 59)
#define GITS_BASER_RaWt			(2UL << 59)
#define GITS_BASER_RaWb			(3UL << 59)
#define GITS_BASER_WaWt			(4UL << 59)
#define GITS_BASER_WaWb			(5UL << 59)
#define GITS_BASER_RaWaWt		(6UL << 59)
#define GITS_BASER_RaWaWb		(7UL << 59)
#define GITS_BASER_TYPE_SHIFT		(56)
#define GITS_BASER_TYPE(r)		(((r) >> GITS_BASER_TYPE_SHIFT) & 7)
#define GITS_BASER_ENTRY_SIZE_SHIFT	(48)
#define GITS_BASER_ENTRY_SIZE(r)	((((r) >> GITS_BASER_ENTRY_SIZE_SHIFT) & 0xff) + 1)
#define GITS_BASER_NonShareable		(0UL << 10)
#define GITS_BASER_InnerShareable	(1UL << 10)
#define GITS_BASER_OuterShareable	(2UL << 10)
#define GITS_BASER_SHAREABILITY_SHIFT	(10)
#define GITS_BASER_SHAREABILITY_MASK	(3UL << GITS_BASER_SHAREABILITY_SHIFT)
#define GITS_BASER_PAGE_SIZE_SHIFT	(8)
#define GITS_BASER_PAGE_SIZE_4K		(0UL << GITS_BASER_PAGE_SIZE_SHIFT)
#define GITS_BASER_PAGE_SIZE_16K	(1UL << GITS_BASER_PAGE_SIZE_SHIFT)
#define GITS_BASER_PAGE_SIZE_64K	(2UL << GITS_BASER_PAGE_SIZE_SHIFT)
#define GITS_BASER_PAGE_SIZE_MASK	(3UL << GITS_BASER_PAGE_SIZE_SHIFT)

#define GITS_BASER_TYPE_NONE		0
#define GITS_BASER_TYPE_DEVICE		1
#define GITS_BASER_TYPE_VCPU		2
#define GITS_BASER_TYPE_CPU		3
#define GITS_BASER_TYPE_COLLECTION	4
#define GITS_BASER_TYPE_RESERVED5	5
#define GITS_BASER_TYPE_RESERVED6	6
#define GITS_BASER_TYPE_RESERVED7	7

/*
 * ITS commands
 */
#define GITS_CMD_MAPD			0x08
#define GITS_CMD_MAPC			0x09
#define GITS_CMD_MAPVI			0x0a
#define GITS_CMD_MOVI			0x01
#define GITS_CMD_DISCARD		0x0f
#define GITS_CMD_INV			0x0c
#define GITS_CMD_MOVALL			0x0e
#define GITS_CMD_INVALL			0x0d
#define GITS_CMD_INT			0x03
#define GITS_CMD_CLEAR			0x04
#define GITS_CMD_SYNC			0x05

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

/*
 * We need a value to serve as a irq-type for LPIs. Choose one that will
 * hopefully pique the interest of the reviewer.
 */
#define GIC_IRQ_TYPE_LPI		0xa110c8ed

struct rdists {
	struct {
		void __iomem	*rd_base;
		struct page	*pend_page;
		phys_addr_t	phys_base;
	} __percpu		*rdist;
	struct page		*prop_page;
	int			id_bits;
	u64			flags;
};

static inline void gic_write_eoir(u64 irq)
{
	asm volatile("msr_s " __stringify(ICC_EOIR1_EL1) ", %0" : : "r" (irq));
	isb();
}

struct irq_domain;
int its_cpu_init(void);
int its_init(struct device_node *node, struct rdists *rdists,
	     struct irq_domain *domain);

#endif

#endif
