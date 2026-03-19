/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __SELFTESTS_GIC_V5_H
#define __SELFTESTS_GIC_V5_H

#include <asm/barrier.h>
#include <asm/sysreg.h>

#include <linux/bitfield.h>

#include "processor.h"

/*
 * Definitions for GICv5 instructions for the Current Domain
 */
#define GICV5_OP_GIC_CDAFF		sys_insn(1, 0, 12, 1, 3)
#define GICV5_OP_GIC_CDDI		sys_insn(1, 0, 12, 2, 0)
#define GICV5_OP_GIC_CDDIS		sys_insn(1, 0, 12, 1, 0)
#define GICV5_OP_GIC_CDHM		sys_insn(1, 0, 12, 2, 1)
#define GICV5_OP_GIC_CDEN		sys_insn(1, 0, 12, 1, 1)
#define GICV5_OP_GIC_CDEOI		sys_insn(1, 0, 12, 1, 7)
#define GICV5_OP_GIC_CDPEND		sys_insn(1, 0, 12, 1, 4)
#define GICV5_OP_GIC_CDPRI		sys_insn(1, 0, 12, 1, 2)
#define GICV5_OP_GIC_CDRCFG		sys_insn(1, 0, 12, 1, 5)
#define GICV5_OP_GICR_CDIA		sys_insn(1, 0, 12, 3, 0)
#define GICV5_OP_GICR_CDNMIA		sys_insn(1, 0, 12, 3, 1)

/* Definitions for GIC CDAFF */
#define GICV5_GIC_CDAFF_IAFFID_MASK	GENMASK_ULL(47, 32)
#define GICV5_GIC_CDAFF_TYPE_MASK	GENMASK_ULL(31, 29)
#define GICV5_GIC_CDAFF_IRM_MASK	BIT_ULL(28)
#define GICV5_GIC_CDAFF_ID_MASK		GENMASK_ULL(23, 0)

/* Definitions for GIC CDDI */
#define GICV5_GIC_CDDI_TYPE_MASK	GENMASK_ULL(31, 29)
#define GICV5_GIC_CDDI_ID_MASK		GENMASK_ULL(23, 0)

/* Definitions for GIC CDDIS */
#define GICV5_GIC_CDDIS_TYPE_MASK	GENMASK_ULL(31, 29)
#define GICV5_GIC_CDDIS_TYPE(r)		FIELD_GET(GICV5_GIC_CDDIS_TYPE_MASK, r)
#define GICV5_GIC_CDDIS_ID_MASK		GENMASK_ULL(23, 0)
#define GICV5_GIC_CDDIS_ID(r)		FIELD_GET(GICV5_GIC_CDDIS_ID_MASK, r)

/* Definitions for GIC CDEN */
#define GICV5_GIC_CDEN_TYPE_MASK	GENMASK_ULL(31, 29)
#define GICV5_GIC_CDEN_ID_MASK		GENMASK_ULL(23, 0)

/* Definitions for GIC CDHM */
#define GICV5_GIC_CDHM_HM_MASK		BIT_ULL(32)
#define GICV5_GIC_CDHM_TYPE_MASK	GENMASK_ULL(31, 29)
#define GICV5_GIC_CDHM_ID_MASK		GENMASK_ULL(23, 0)

/* Definitions for GIC CDPEND */
#define GICV5_GIC_CDPEND_PENDING_MASK	BIT_ULL(32)
#define GICV5_GIC_CDPEND_TYPE_MASK	GENMASK_ULL(31, 29)
#define GICV5_GIC_CDPEND_ID_MASK	GENMASK_ULL(23, 0)

/* Definitions for GIC CDPRI */
#define GICV5_GIC_CDPRI_PRIORITY_MASK	GENMASK_ULL(39, 35)
#define GICV5_GIC_CDPRI_TYPE_MASK	GENMASK_ULL(31, 29)
#define GICV5_GIC_CDPRI_ID_MASK		GENMASK_ULL(23, 0)

/* Definitions for GIC CDRCFG */
#define GICV5_GIC_CDRCFG_TYPE_MASK	GENMASK_ULL(31, 29)
#define GICV5_GIC_CDRCFG_ID_MASK	GENMASK_ULL(23, 0)

/* Definitions for GICR CDIA */
#define GICV5_GICR_CDIA_VALID_MASK	BIT_ULL(32)
#define GICV5_GICR_CDIA_VALID(r)	FIELD_GET(GICV5_GICR_CDIA_VALID_MASK, r)
#define GICV5_GICR_CDIA_TYPE_MASK	GENMASK_ULL(31, 29)
#define GICV5_GICR_CDIA_ID_MASK		GENMASK_ULL(23, 0)
#define GICV5_GICR_CDIA_INTID		GENMASK_ULL(31, 0)

/* Definitions for GICR CDNMIA */
#define GICV5_GICR_CDNMIA_VALID_MASK	BIT_ULL(32)
#define GICV5_GICR_CDNMIA_VALID(r)	FIELD_GET(GICV5_GICR_CDNMIA_VALID_MASK, r)
#define GICV5_GICR_CDNMIA_TYPE_MASK	GENMASK_ULL(31, 29)
#define GICV5_GICR_CDNMIA_ID_MASK	GENMASK_ULL(23, 0)

#define gicr_insn(insn)			read_sysreg_s(GICV5_OP_GICR_##insn)
#define gic_insn(v, insn)		write_sysreg_s(v, GICV5_OP_GIC_##insn)

#define __GIC_BARRIER_INSN(op0, op1, CRn, CRm, op2, Rt)			\
	__emit_inst(0xd5000000					|	\
		    sys_insn((op0), (op1), (CRn), (CRm), (op2))	|	\
		    ((Rt) & 0x1f))

#define GSB_SYS_BARRIER_INSN		__GIC_BARRIER_INSN(1, 0, 12, 0, 0, 31)
#define GSB_ACK_BARRIER_INSN		__GIC_BARRIER_INSN(1, 0, 12, 0, 1, 31)

#define gsb_ack()	asm volatile(GSB_ACK_BARRIER_INSN : : : "memory")
#define gsb_sys()	asm volatile(GSB_SYS_BARRIER_INSN : : : "memory")

#define REPEAT_BYTE(x)	((~0ul / 0xff) * (x))

#define GICV5_IRQ_DEFAULT_PRI 0b10000

#define GICV5_ARCH_PPI_SW_PPI		0x3

void gicv5_ppi_priority_init(void)
{
	write_sysreg_s(REPEAT_BYTE(GICV5_IRQ_DEFAULT_PRI), SYS_ICC_PPI_PRIORITYR0_EL1);
	write_sysreg_s(REPEAT_BYTE(GICV5_IRQ_DEFAULT_PRI), SYS_ICC_PPI_PRIORITYR1_EL1);
	write_sysreg_s(REPEAT_BYTE(GICV5_IRQ_DEFAULT_PRI), SYS_ICC_PPI_PRIORITYR2_EL1);
	write_sysreg_s(REPEAT_BYTE(GICV5_IRQ_DEFAULT_PRI), SYS_ICC_PPI_PRIORITYR3_EL1);
	write_sysreg_s(REPEAT_BYTE(GICV5_IRQ_DEFAULT_PRI), SYS_ICC_PPI_PRIORITYR4_EL1);
	write_sysreg_s(REPEAT_BYTE(GICV5_IRQ_DEFAULT_PRI), SYS_ICC_PPI_PRIORITYR5_EL1);
	write_sysreg_s(REPEAT_BYTE(GICV5_IRQ_DEFAULT_PRI), SYS_ICC_PPI_PRIORITYR6_EL1);
	write_sysreg_s(REPEAT_BYTE(GICV5_IRQ_DEFAULT_PRI), SYS_ICC_PPI_PRIORITYR7_EL1);
	write_sysreg_s(REPEAT_BYTE(GICV5_IRQ_DEFAULT_PRI), SYS_ICC_PPI_PRIORITYR8_EL1);
	write_sysreg_s(REPEAT_BYTE(GICV5_IRQ_DEFAULT_PRI), SYS_ICC_PPI_PRIORITYR9_EL1);
	write_sysreg_s(REPEAT_BYTE(GICV5_IRQ_DEFAULT_PRI), SYS_ICC_PPI_PRIORITYR10_EL1);
	write_sysreg_s(REPEAT_BYTE(GICV5_IRQ_DEFAULT_PRI), SYS_ICC_PPI_PRIORITYR11_EL1);
	write_sysreg_s(REPEAT_BYTE(GICV5_IRQ_DEFAULT_PRI), SYS_ICC_PPI_PRIORITYR12_EL1);
	write_sysreg_s(REPEAT_BYTE(GICV5_IRQ_DEFAULT_PRI), SYS_ICC_PPI_PRIORITYR13_EL1);
	write_sysreg_s(REPEAT_BYTE(GICV5_IRQ_DEFAULT_PRI), SYS_ICC_PPI_PRIORITYR14_EL1);
	write_sysreg_s(REPEAT_BYTE(GICV5_IRQ_DEFAULT_PRI), SYS_ICC_PPI_PRIORITYR15_EL1);

	/*
	 * Context syncronization required to make sure system register writes
	 * effects are synchronised.
	 */
	isb();
}

void gicv5_cpu_disable_interrupts(void)
{
	u64 cr0;

	cr0 = FIELD_PREP(ICC_CR0_EL1_EN, 0);
	write_sysreg_s(cr0, SYS_ICC_CR0_EL1);
}

void gicv5_cpu_enable_interrupts(void)
{
	u64 cr0, pcr;

	write_sysreg_s(0, SYS_ICC_PPI_ENABLER0_EL1);
	write_sysreg_s(0, SYS_ICC_PPI_ENABLER1_EL1);

	gicv5_ppi_priority_init();

	pcr = FIELD_PREP(ICC_PCR_EL1_PRIORITY, GICV5_IRQ_DEFAULT_PRI);
	write_sysreg_s(pcr, SYS_ICC_PCR_EL1);

	cr0 = FIELD_PREP(ICC_CR0_EL1_EN, 1);
	write_sysreg_s(cr0, SYS_ICC_CR0_EL1);
}

#endif
