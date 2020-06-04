/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _PKEYS_POWERPC_H
#define _PKEYS_POWERPC_H

#ifndef SYS_mprotect_key
# define SYS_mprotect_key	386
#endif
#ifndef SYS_pkey_alloc
# define SYS_pkey_alloc		384
# define SYS_pkey_free		385
#endif
#define REG_IP_IDX		PT_NIP
#define REG_TRAPNO		PT_TRAP
#define gregs			gp_regs
#define fpregs			fp_regs
#define si_pkey_offset		0x20

#ifndef PKEY_DISABLE_ACCESS
# define PKEY_DISABLE_ACCESS	0x3  /* disable read and write */
#endif

#ifndef PKEY_DISABLE_WRITE
# define PKEY_DISABLE_WRITE	0x2
#endif

#define NR_PKEYS		32
#define NR_RESERVED_PKEYS_4K	27 /* pkey-0, pkey-1, exec-only-pkey
				      and 24 other keys that cannot be
				      represented in the PTE */
#define NR_RESERVED_PKEYS_64K	3  /* pkey-0, pkey-1 and exec-only-pkey */
#define PKEY_BITS_PER_PKEY	2
#define HPAGE_SIZE		(1UL << 24)
#define PAGE_SIZE		(1UL << 16)

static inline u32 pkey_bit_position(int pkey)
{
	return (NR_PKEYS - pkey - 1) * PKEY_BITS_PER_PKEY;
}

static inline u64 __read_pkey_reg(void)
{
	u64 pkey_reg;

	asm volatile("mfspr %0, 0xd" : "=r" (pkey_reg));

	return pkey_reg;
}

static inline void __write_pkey_reg(u64 pkey_reg)
{
	u64 amr = pkey_reg;

	dprintf4("%s() changing %016llx to %016llx\n",
			 __func__, __read_pkey_reg(), pkey_reg);

	asm volatile("isync; mtspr 0xd, %0; isync"
		     : : "r" ((unsigned long)(amr)) : "memory");

	dprintf4("%s() pkey register after changing %016llx to %016llx\n",
			__func__, __read_pkey_reg(), pkey_reg);
}

static inline int cpu_has_pku(void)
{
	return 1;
}

static inline int get_arch_reserved_keys(void)
{
	if (sysconf(_SC_PAGESIZE) == 4096)
		return NR_RESERVED_PKEYS_4K;
	else
		return NR_RESERVED_PKEYS_64K;
}

void expect_fault_on_read_execonly_key(void *p1, int pkey)
{
	/*
	 * powerpc does not allow userspace to change permissions of exec-only
	 * keys since those keys are not allocated by userspace. The signal
	 * handler wont be able to reset the permissions, which means the code
	 * will infinitely continue to segfault here.
	 */
	return;
}

/* 4-byte instructions * 16384 = 64K page */
#define __page_o_noops() asm(".rept 16384 ; nop; .endr")

#endif /* _PKEYS_POWERPC_H */
