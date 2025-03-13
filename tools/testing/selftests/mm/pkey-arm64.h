/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2023 Arm Ltd.
 */

#ifndef _PKEYS_ARM64_H
#define _PKEYS_ARM64_H

#include "vm_util.h"
/* for signal frame parsing */
#include "../arm64/signal/testcases/testcases.h"

#ifndef SYS_mprotect_key
# define SYS_mprotect_key	288
#endif
#ifndef SYS_pkey_alloc
# define SYS_pkey_alloc		289
# define SYS_pkey_free		290
#endif
#define MCONTEXT_IP(mc)		mc.pc
#define MCONTEXT_TRAPNO(mc)	-1

#define PKEY_MASK		0xf

#define POE_NONE		0x0
#define POE_X			0x2
#define POE_RX			0x3
#define POE_RWX			0x7

#define NR_PKEYS		8
#define NR_RESERVED_PKEYS	1 /* pkey-0 */

#define PKEY_REG_ALLOW_ALL	0x77777777
#define PKEY_REG_ALLOW_NONE	0x0

#define PKEY_BITS_PER_PKEY	4
#define PAGE_SIZE		sysconf(_SC_PAGESIZE)
#undef HPAGE_SIZE
#define HPAGE_SIZE		default_huge_page_size()

/* 4-byte instructions * 16384 = 64K page */
#define __page_o_noops() asm(".rept 16384 ; nop; .endr")

static inline u64 __read_pkey_reg(void)
{
	u64 pkey_reg = 0;

	// POR_EL0
	asm volatile("mrs %0, S3_3_c10_c2_4" : "=r" (pkey_reg));

	return pkey_reg;
}

static inline void __write_pkey_reg(u64 pkey_reg)
{
	u64 por = pkey_reg;

	dprintf4("%s() changing %016llx to %016llx\n",
			 __func__, __read_pkey_reg(), pkey_reg);

	// POR_EL0
	asm volatile("msr S3_3_c10_c2_4, %0\nisb" :: "r" (por) :);

	dprintf4("%s() pkey register after changing %016llx to %016llx\n",
			__func__, __read_pkey_reg(), pkey_reg);
}

static inline int cpu_has_pkeys(void)
{
	/* No simple way to determine this */
	return 1;
}

static inline u32 pkey_bit_position(int pkey)
{
	return pkey * PKEY_BITS_PER_PKEY;
}

static inline int get_arch_reserved_keys(void)
{
	return NR_RESERVED_PKEYS;
}

static inline void expect_fault_on_read_execonly_key(void *p1, int pkey)
{
}

static inline void *malloc_pkey_with_mprotect_subpage(long size, int prot, u16 pkey)
{
	return PTR_ERR_ENOTSUP;
}

#define set_pkey_bits	set_pkey_bits
static inline u64 set_pkey_bits(u64 reg, int pkey, u64 flags)
{
	u32 shift = pkey_bit_position(pkey);
	u64 new_val = POE_RWX;

	/* mask out bits from pkey in old value */
	reg &= ~((u64)PKEY_MASK << shift);

	if (flags & PKEY_DISABLE_ACCESS)
		new_val = POE_X;
	else if (flags & PKEY_DISABLE_WRITE)
		new_val = POE_RX;

	/* OR in new bits for pkey */
	reg |= new_val << shift;

	return reg;
}

#define get_pkey_bits	get_pkey_bits
static inline u64 get_pkey_bits(u64 reg, int pkey)
{
	u32 shift = pkey_bit_position(pkey);
	/*
	 * shift down the relevant bits to the lowest four, then
	 * mask off all the other higher bits
	 */
	u32 perm = (reg >> shift) & PKEY_MASK;

	if (perm == POE_X)
		return PKEY_DISABLE_ACCESS;
	if (perm == POE_RX)
		return PKEY_DISABLE_WRITE;
	return 0;
}

static inline void aarch64_write_signal_pkey(ucontext_t *uctxt, u64 pkey)
{
	struct _aarch64_ctx *ctx = GET_UC_RESV_HEAD(uctxt);
	struct poe_context *poe_ctx =
		(struct poe_context *) get_header(ctx, POE_MAGIC,
						sizeof(uctxt->uc_mcontext), NULL);
	if (poe_ctx)
		poe_ctx->por_el0 = pkey;
}

#endif /* _PKEYS_ARM64_H */
