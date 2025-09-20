/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef SELFTEST_KVM_PROCESSOR_H
#define SELFTEST_KVM_PROCESSOR_H

#ifndef __ASSEMBLER__
#include "ucall_common.h"

#else
/* general registers */
#define zero				$r0
#define ra				$r1
#define tp				$r2
#define sp				$r3
#define a0				$r4
#define a1				$r5
#define a2				$r6
#define a3				$r7
#define a4				$r8
#define a5				$r9
#define a6				$r10
#define a7				$r11
#define t0				$r12
#define t1				$r13
#define t2				$r14
#define t3				$r15
#define t4				$r16
#define t5				$r17
#define t6				$r18
#define t7				$r19
#define t8				$r20
#define u0				$r21
#define fp				$r22
#define s0				$r23
#define s1				$r24
#define s2				$r25
#define s3				$r26
#define s4				$r27
#define s5				$r28
#define s6				$r29
#define s7				$r30
#define s8				$r31
#endif

/*
 * LoongArch page table entry definition
 * Original header file arch/loongarch/include/asm/loongarch.h
 */
#define _PAGE_VALID_SHIFT		0
#define _PAGE_DIRTY_SHIFT		1
#define _PAGE_PLV_SHIFT			2  /* 2~3, two bits */
#define  PLV_KERN			0
#define  PLV_USER			3
#define  PLV_MASK			0x3
#define _CACHE_SHIFT			4  /* 4~5, two bits */
#define _PAGE_PRESENT_SHIFT		7
#define _PAGE_WRITE_SHIFT		8

#define _PAGE_VALID			BIT_ULL(_PAGE_VALID_SHIFT)
#define _PAGE_PRESENT			BIT_ULL(_PAGE_PRESENT_SHIFT)
#define _PAGE_WRITE			BIT_ULL(_PAGE_WRITE_SHIFT)
#define _PAGE_DIRTY			BIT_ULL(_PAGE_DIRTY_SHIFT)
#define _PAGE_USER			(PLV_USER << _PAGE_PLV_SHIFT)
#define   __READABLE			(_PAGE_VALID)
#define   __WRITEABLE			(_PAGE_DIRTY | _PAGE_WRITE)
/* Coherent Cached */
#define _CACHE_CC			BIT_ULL(_CACHE_SHIFT)
#define PS_4K				0x0000000c
#define PS_16K				0x0000000e
#define PS_64K				0x00000010
#define PS_DEFAULT_SIZE			PS_16K

/* LoongArch Basic CSR registers */
#define LOONGARCH_CSR_CRMD		0x0 /* Current mode info */
#define  CSR_CRMD_PG_SHIFT		4
#define  CSR_CRMD_PG			BIT_ULL(CSR_CRMD_PG_SHIFT)
#define  CSR_CRMD_IE_SHIFT		2
#define  CSR_CRMD_IE			BIT_ULL(CSR_CRMD_IE_SHIFT)
#define  CSR_CRMD_PLV_SHIFT		0
#define  CSR_CRMD_PLV_WIDTH		2
#define  CSR_CRMD_PLV			(0x3UL << CSR_CRMD_PLV_SHIFT)
#define  PLV_MASK			0x3
#define LOONGARCH_CSR_PRMD		0x1
#define LOONGARCH_CSR_EUEN		0x2
#define LOONGARCH_CSR_ECFG		0x4
#define LOONGARCH_CSR_ESTAT		0x5  /* Exception status */
#define LOONGARCH_CSR_ERA		0x6  /* ERA */
#define LOONGARCH_CSR_BADV		0x7  /* Bad virtual address */
#define LOONGARCH_CSR_EENTRY		0xc
#define LOONGARCH_CSR_TLBIDX		0x10 /* TLB Index, EHINV, PageSize */
#define  CSR_TLBIDX_PS_SHIFT		24
#define  CSR_TLBIDX_PS_WIDTH		6
#define  CSR_TLBIDX_PS			(0x3fUL << CSR_TLBIDX_PS_SHIFT)
#define  CSR_TLBIDX_SIZEM		0x3f000000
#define  CSR_TLBIDX_SIZE		CSR_TLBIDX_PS_SHIFT
#define LOONGARCH_CSR_ASID		0x18 /* ASID */
#define LOONGARCH_CSR_PGDL		0x19
#define LOONGARCH_CSR_PGDH		0x1a
/* Page table base */
#define LOONGARCH_CSR_PGD		0x1b
#define LOONGARCH_CSR_PWCTL0		0x1c
#define LOONGARCH_CSR_PWCTL1		0x1d
#define LOONGARCH_CSR_STLBPGSIZE	0x1e
#define LOONGARCH_CSR_CPUID		0x20
#define LOONGARCH_CSR_KS0		0x30
#define LOONGARCH_CSR_KS1		0x31
#define LOONGARCH_CSR_TMID		0x40
#define LOONGARCH_CSR_TCFG		0x41
/* TLB refill exception entry */
#define LOONGARCH_CSR_TLBRENTRY		0x88
#define LOONGARCH_CSR_TLBRSAVE		0x8b
#define LOONGARCH_CSR_TLBREHI		0x8e
#define  CSR_TLBREHI_PS_SHIFT		0
#define  CSR_TLBREHI_PS			(0x3fUL << CSR_TLBREHI_PS_SHIFT)

#define EXREGS_GPRS			(32)

#ifndef __ASSEMBLER__
void handle_tlb_refill(void);
void handle_exception(void);

struct ex_regs {
	unsigned long regs[EXREGS_GPRS];
	unsigned long pc;
	unsigned long estat;
	unsigned long badv;
};

#define PC_OFFSET_EXREGS		offsetof(struct ex_regs, pc)
#define ESTAT_OFFSET_EXREGS		offsetof(struct ex_regs, estat)
#define BADV_OFFSET_EXREGS		offsetof(struct ex_regs, badv)
#define EXREGS_SIZE			sizeof(struct ex_regs)

#else
#define PC_OFFSET_EXREGS		((EXREGS_GPRS + 0) * 8)
#define ESTAT_OFFSET_EXREGS		((EXREGS_GPRS + 1) * 8)
#define BADV_OFFSET_EXREGS		((EXREGS_GPRS + 2) * 8)
#define EXREGS_SIZE			((EXREGS_GPRS + 3) * 8)
#endif

#endif /* SELFTEST_KVM_PROCESSOR_H */
