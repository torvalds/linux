/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _PKEYS_X86_H
#define _PKEYS_X86_H

#ifdef __i386__

#define REG_IP_IDX		REG_EIP
#define si_pkey_offset		0x14

#else

#define REG_IP_IDX		REG_RIP
#define si_pkey_offset		0x20

#endif

#define MCONTEXT_IP(mc)		mc.gregs[REG_IP_IDX]
#define MCONTEXT_TRAPNO(mc)	mc.gregs[REG_TRAPNO]
#define MCONTEXT_FPREGS

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

/* Intel-defined CPU features, CPUID level 0x00000007:0 (ecx) */
#define X86_FEATURE_PKU        (1<<3) /* Protection Keys for Userspace */
#define X86_FEATURE_OSPKE      (1<<4) /* OS Protection Keys Enable */

static inline int cpu_has_pkeys(void)
{
	unsigned int eax;
	unsigned int ebx;
	unsigned int ecx;
	unsigned int edx;

	__cpuid_count(0x7, 0x0, eax, ebx, ecx, edx);

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

static inline int cpu_max_xsave_size(void)
{
	unsigned long XSTATE_CPUID = 0xd;
	unsigned int eax;
	unsigned int ebx;
	unsigned int ecx;
	unsigned int edx;

	__cpuid_count(XSTATE_CPUID, 0, eax, ebx, ecx, edx);
	return ecx;
}

static inline u32 pkey_bit_position(int pkey)
{
	return pkey * PKEY_BITS_PER_PKEY;
}

#define XSTATE_PKEY_BIT	(9)
#define XSTATE_PKEY	0x200
#define XSTATE_BV_OFFSET	512

int pkey_reg_xstate_offset(void)
{
	unsigned int eax;
	unsigned int ebx;
	unsigned int ecx;
	unsigned int edx;
	int xstate_offset;
	int xstate_size = 0;
	unsigned long XSTATE_CPUID = 0xd;
	int leaf;

	/* assume that XSTATE_PKEY is set in XCR0 */
	leaf = XSTATE_PKEY_BIT;
	{
		__cpuid_count(XSTATE_CPUID, leaf, eax, ebx, ecx, edx);

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
