/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _PKEYS_X86_H
#define _PKEYS_X86_H

#ifdef __i386__

#ifndef SYS_mprotect_key
# define SYS_mprotect_key	380
#endif

#ifndef SYS_pkey_alloc
# define SYS_pkey_alloc		381
# define SYS_pkey_free		382
#endif

#define REG_IP_IDX		REG_EIP
#define si_pkey_offset		0x14

#else

#ifndef SYS_mprotect_key
# define SYS_mprotect_key	329
#endif

#ifndef SYS_pkey_alloc
# define SYS_pkey_alloc		330
# define SYS_pkey_free		331
#endif

#define REG_IP_IDX		REG_RIP
#define si_pkey_offset		0x20

#endif

#ifndef PKEY_DISABLE_ACCESS
# define PKEY_DISABLE_ACCESS	0x1
#endif

#ifndef PKEY_DISABLE_WRITE
# define PKEY_DISABLE_WRITE	0x2
#endif

#define NR_PKEYS		16
#define NR_RESERVED_PKEYS	2 /* pkey-0 and exec-only-pkey */
#define PKEY_BITS_PER_PKEY	2
#define HPAGE_SIZE		(1UL<<21)
#define PAGE_SIZE		4096
#define MB			(1<<20)

static inline void __page_o_noops(void)
{
	/* 8-bytes of instruction * 512 bytes = 1 page */
	asm(".rept 512 ; nopl 0x7eeeeeee(%eax) ; .endr");
}

static inline u64 __read_pkey_reg(void)
{
	unsigned int eax, edx;
	unsigned int ecx = 0;
	unsigned pkey_reg;

	asm volatile(".byte 0x0f,0x01,0xee\n\t"
		     : "=a" (eax), "=d" (edx)
		     : "c" (ecx));
	pkey_reg = eax;
	return pkey_reg;
}

static inline void __write_pkey_reg(u64 pkey_reg)
{
	unsigned int eax = pkey_reg;
	unsigned int ecx = 0;
	unsigned int edx = 0;

	dprintf4("%s() changing %016llx to %016llx\n", __func__,
			__read_pkey_reg(), pkey_reg);
	asm volatile(".byte 0x0f,0x01,0xef\n\t"
		     : : "a" (eax), "c" (ecx), "d" (edx));
	assert(pkey_reg == __read_pkey_reg());
}

static inline void __cpuid(unsigned int *eax, unsigned int *ebx,
		unsigned int *ecx, unsigned int *edx)
{
	/* ecx is often an input as well as an output. */
	asm volatile(
		"cpuid;"
		: "=a" (*eax),
		  "=b" (*ebx),
		  "=c" (*ecx),
		  "=d" (*edx)
		: "0" (*eax), "2" (*ecx));
}

/* Intel-defined CPU features, CPUID level 0x00000007:0 (ecx) */
#define X86_FEATURE_PKU        (1<<3) /* Protection Keys for Userspace */
#define X86_FEATURE_OSPKE      (1<<4) /* OS Protection Keys Enable */

static inline int cpu_has_pkeys(void)
{
	unsigned int eax;
	unsigned int ebx;
	unsigned int ecx;
	unsigned int edx;

	eax = 0x7;
	ecx = 0x0;
	__cpuid(&eax, &ebx, &ecx, &edx);

	if (!(ecx & X86_FEATURE_PKU)) {
		dprintf2("cpu does not have PKU\n");
		return 0;
	}
	if (!(ecx & X86_FEATURE_OSPKE)) {
		dprintf2("cpu does not have OSPKE\n");
		return 0;
	}
	return 1;
}

static inline u32 pkey_bit_position(int pkey)
{
	return pkey * PKEY_BITS_PER_PKEY;
}

#define XSTATE_PKEY_BIT	(9)
#define XSTATE_PKEY	0x200

int pkey_reg_xstate_offset(void)
{
	unsigned int eax;
	unsigned int ebx;
	unsigned int ecx;
	unsigned int edx;
	int xstate_offset;
	int xstate_size;
	unsigned long XSTATE_CPUID = 0xd;
	int leaf;

	/* assume that XSTATE_PKEY is set in XCR0 */
	leaf = XSTATE_PKEY_BIT;
	{
		eax = XSTATE_CPUID;
		ecx = leaf;
		__cpuid(&eax, &ebx, &ecx, &edx);

		if (leaf == XSTATE_PKEY_BIT) {
			xstate_offset = ebx;
			xstate_size = eax;
		}
	}

	if (xstate_size == 0) {
		printf("could not find size/offset of PKEY in xsave state\n");
		return 0;
	}

	return xstate_offset;
}

static inline int get_arch_reserved_keys(void)
{
	return NR_RESERVED_PKEYS;
}

void expect_fault_on_read_execonly_key(void *p1, int pkey)
{
	int ptr_contents;

	ptr_contents = read_ptr(p1);
	dprintf2("ptr (%p) contents@%d: %x\n", p1, __LINE__, ptr_contents);
	expected_pkey_fault(pkey);
}

void *malloc_pkey_with_mprotect_subpage(long size, int prot, u16 pkey)
{
	return PTR_ERR_ENOTSUP;
}

#endif /* _PKEYS_X86_H */
