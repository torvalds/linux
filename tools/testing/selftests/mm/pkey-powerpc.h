/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _PKEYS_POWERPC_H
#define _PKEYS_POWERPC_H

#ifndef SYS_pkey_alloc
# define SYS_pkey_alloc		384
# define SYS_pkey_free		385
#endif
#define REG_IP_IDX		PT_NIP
#define REG_TRAPNO		PT_TRAP
#define gregs			gp_regs
#define fpregs			fp_regs
#define si_pkey_offset		0x20

#undef PKEY_DISABLE_ACCESS
#define PKEY_DISABLE_ACCESS	0x3  /* disable read and write */

#undef PKEY_DISABLE_WRITE
#define PKEY_DISABLE_WRITE	0x2

#define NR_PKEYS		32
#define NR_RESERVED_PKEYS_4K	27 /* pkey-0, pkey-1, exec-only-pkey
				      and 24 other keys that cannot be
				      represented in the PTE */
#define NR_RESERVED_PKEYS_64K_3KEYS	3 /* PowerNV and KVM: pkey-0,
					     pkey-1 and exec-only key */
#define NR_RESERVED_PKEYS_64K_4KEYS	4 /* PowerVM: pkey-0, pkey-1,
					     pkey-31 and exec-only key */
#define PKEY_BITS_PER_PKEY	2
#define HPAGE_SIZE		(1UL << 24)
#define PAGE_SIZE		sysconf(_SC_PAGESIZE)

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

static inline int cpu_has_pkeys(void)
{
	/* No simple way to determine this */
	return 1;
}

static inline bool arch_is_powervm()
{
	struct stat buf;

	if ((stat("/sys/firmware/devicetree/base/ibm,partition-name", &buf) == 0) &&
	    (stat("/sys/firmware/devicetree/base/hmc-managed?", &buf) == 0) &&
	    (stat("/sys/firmware/devicetree/base/chosen/qemu,graphic-width", &buf) == -1) )
		return true;

	return false;
}

static inline int get_arch_reserved_keys(void)
{
	if (sysconf(_SC_PAGESIZE) == 4096)
		return NR_RESERVED_PKEYS_4K;
	else
		if (arch_is_powervm())
			return NR_RESERVED_PKEYS_64K_4KEYS;
		else
			return NR_RESERVED_PKEYS_64K_3KEYS;
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

void *malloc_pkey_with_mprotect_subpage(long size, int prot, u16 pkey)
{
	void *ptr;
	int ret;

	dprintf1("doing %s(size=%ld, prot=0x%x, pkey=%d)\n", __func__,
			size, prot, pkey);
	pkey_assert(pkey < NR_PKEYS);
	ptr = mmap(NULL, size, prot, MAP_ANONYMOUS|MAP_PRIVATE, -1, 0);
	pkey_assert(ptr != (void *)-1);

	ret = syscall(__NR_subpage_prot, ptr, size, NULL);
	if (ret) {
		perror("subpage_perm");
		return PTR_ERR_ENOTSUP;
	}

	ret = mprotect_pkey((void *)ptr, PAGE_SIZE, prot, pkey);
	pkey_assert(!ret);
	record_pkey_malloc(ptr, size, prot);

	dprintf1("%s() for pkey %d @ %p\n", __func__, pkey, ptr);
	return ptr;
}

#endif /* _PKEYS_POWERPC_H */
