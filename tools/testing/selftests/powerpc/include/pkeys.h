/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2020, Sandipan Das, IBM Corp.
 */

#ifndef _SELFTESTS_POWERPC_PKEYS_H
#define _SELFTESTS_POWERPC_PKEYS_H

#include <sys/mman.h>

#include "reg.h"
#include "utils.h"

/*
 * Older versions of libc use the Intel-specific access rights.
 * Hence, override the definitions as they might be incorrect.
 */
#undef PKEY_DISABLE_ACCESS
#define PKEY_DISABLE_ACCESS	0x3

#undef PKEY_DISABLE_WRITE
#define PKEY_DISABLE_WRITE	0x2

#undef PKEY_DISABLE_EXECUTE
#define PKEY_DISABLE_EXECUTE	0x4

#undef PKEY_UNRESTRICTED
#define PKEY_UNRESTRICTED	0x0

/* Older versions of libc do not define this */
#ifndef SEGV_PKUERR
#define SEGV_PKUERR	4
#endif

#define SI_PKEY_OFFSET	0x20

#define __NR_pkey_mprotect	386
#define __NR_pkey_alloc		384
#define __NR_pkey_free		385

#ifndef NT_PPC_PKEY
#define NT_PPC_PKEY		0x110
#endif

#define PKEY_BITS_PER_PKEY	2
#define NR_PKEYS		32
#define PKEY_BITS_MASK		((1UL << PKEY_BITS_PER_PKEY) - 1)

#define AMR_BITS_PER_PKEY 2
#define PKEY_REG_BITS (sizeof(u64) * 8)
#define pkeyshift(pkey) (PKEY_REG_BITS - ((pkey + 1) * AMR_BITS_PER_PKEY))

inline unsigned long pkeyreg_get(void)
{
	return mfspr(SPRN_AMR);
}

inline void pkeyreg_set(unsigned long amr)
{
	set_amr(amr);
}

void pkey_set_rights(int pkey, unsigned long rights)
{
	unsigned long amr, shift;

	shift = (NR_PKEYS - pkey - 1) * PKEY_BITS_PER_PKEY;
	amr = pkeyreg_get();
	amr &= ~(PKEY_BITS_MASK << shift);
	amr |= (rights & PKEY_BITS_MASK) << shift;
	pkeyreg_set(amr);
}

int sys_pkey_mprotect(void *addr, size_t len, int prot, int pkey)
{
	return syscall(__NR_pkey_mprotect, addr, len, prot, pkey);
}

int sys_pkey_alloc(unsigned long flags, unsigned long rights)
{
	return syscall(__NR_pkey_alloc, flags, rights);
}

int sys_pkey_free(int pkey)
{
	return syscall(__NR_pkey_free, pkey);
}

int pkeys_unsupported(void)
{
	bool hash_mmu = false;
	int pkey;

	/* Protection keys are currently supported on Hash MMU only */
	FAIL_IF(using_hash_mmu(&hash_mmu));
	SKIP_IF(!hash_mmu);

	/* Check if the system call is supported */
	pkey = sys_pkey_alloc(0, PKEY_UNRESTRICTED);
	SKIP_IF(pkey < 0);
	sys_pkey_free(pkey);

	return 0;
}

int siginfo_pkey(siginfo_t *si)
{
	/*
	 * In older versions of libc, siginfo_t does not have si_pkey as
	 * a member.
	 */
#ifdef si_pkey
	return si->si_pkey;
#else
	return *((int *)(((char *) si) + SI_PKEY_OFFSET));
#endif
}

#define pkey_rights(r) ({						\
	static char buf[4] = "rwx";					\
	unsigned int amr_bits;						\
	if ((r) & PKEY_DISABLE_EXECUTE)					\
		buf[2] = '-';						\
	amr_bits = (r) & PKEY_BITS_MASK;				\
	if (amr_bits & PKEY_DISABLE_WRITE)				\
		buf[1] = '-';						\
	if (amr_bits & PKEY_DISABLE_ACCESS & ~PKEY_DISABLE_WRITE)	\
		buf[0] = '-';						\
	buf;								\
})

unsigned long next_pkey_rights(unsigned long rights)
{
	if (rights == PKEY_DISABLE_ACCESS)
		return PKEY_DISABLE_EXECUTE;
	else if (rights == (PKEY_DISABLE_ACCESS | PKEY_DISABLE_EXECUTE))
		return 0;

	if ((rights & PKEY_BITS_MASK) == 0)
		rights |= PKEY_DISABLE_WRITE;
	else if ((rights & PKEY_BITS_MASK) == PKEY_DISABLE_WRITE)
		rights |= PKEY_DISABLE_ACCESS;

	return rights;
}

#endif /* _SELFTESTS_POWERPC_PKEYS_H */
