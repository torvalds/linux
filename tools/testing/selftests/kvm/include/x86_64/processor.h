/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * tools/testing/selftests/kvm/include/x86_64/processor.h
 *
 * Copyright (C) 2018, Google LLC.
 */

#ifndef SELFTEST_KVM_PROCESSOR_H
#define SELFTEST_KVM_PROCESSOR_H

#include <assert.h>
#include <stdint.h>
#include <syscall.h>

#include <asm/msr-index.h>
#include <asm/prctl.h>

#include <linux/stringify.h>

#include "../kvm_util.h"

extern bool host_cpu_is_intel;
extern bool host_cpu_is_amd;

#define NMI_VECTOR		0x02

#define X86_EFLAGS_FIXED	 (1u << 1)

#define X86_CR4_VME		(1ul << 0)
#define X86_CR4_PVI		(1ul << 1)
#define X86_CR4_TSD		(1ul << 2)
#define X86_CR4_DE		(1ul << 3)
#define X86_CR4_PSE		(1ul << 4)
#define X86_CR4_PAE		(1ul << 5)
#define X86_CR4_MCE		(1ul << 6)
#define X86_CR4_PGE		(1ul << 7)
#define X86_CR4_PCE		(1ul << 8)
#define X86_CR4_OSFXSR		(1ul << 9)
#define X86_CR4_OSXMMEXCPT	(1ul << 10)
#define X86_CR4_UMIP		(1ul << 11)
#define X86_CR4_LA57		(1ul << 12)
#define X86_CR4_VMXE		(1ul << 13)
#define X86_CR4_SMXE		(1ul << 14)
#define X86_CR4_FSGSBASE	(1ul << 16)
#define X86_CR4_PCIDE		(1ul << 17)
#define X86_CR4_OSXSAVE		(1ul << 18)
#define X86_CR4_SMEP		(1ul << 20)
#define X86_CR4_SMAP		(1ul << 21)
#define X86_CR4_PKE		(1ul << 22)

struct xstate_header {
	u64				xstate_bv;
	u64				xcomp_bv;
	u64				reserved[6];
} __attribute__((packed));

struct xstate {
	u8				i387[512];
	struct xstate_header		header;
	u8				extended_state_area[0];
} __attribute__ ((packed, aligned (64)));

#define XFEATURE_MASK_FP		BIT_ULL(0)
#define XFEATURE_MASK_SSE		BIT_ULL(1)
#define XFEATURE_MASK_YMM		BIT_ULL(2)
#define XFEATURE_MASK_BNDREGS		BIT_ULL(3)
#define XFEATURE_MASK_BNDCSR		BIT_ULL(4)
#define XFEATURE_MASK_OPMASK		BIT_ULL(5)
#define XFEATURE_MASK_ZMM_Hi256		BIT_ULL(6)
#define XFEATURE_MASK_Hi16_ZMM		BIT_ULL(7)
#define XFEATURE_MASK_XTILE_CFG		BIT_ULL(17)
#define XFEATURE_MASK_XTILE_DATA	BIT_ULL(18)

#define XFEATURE_MASK_AVX512		(XFEATURE_MASK_OPMASK | \
					 XFEATURE_MASK_ZMM_Hi256 | \
					 XFEATURE_MASK_Hi16_ZMM)
#define XFEATURE_MASK_XTILE		(XFEATURE_MASK_XTILE_DATA | \
					 XFEATURE_MASK_XTILE_CFG)

/* Note, these are ordered alphabetically to match kvm_cpuid_entry2.  Eww. */
enum cpuid_output_regs {
	KVM_CPUID_EAX,
	KVM_CPUID_EBX,
	KVM_CPUID_ECX,
	KVM_CPUID_EDX
};

/*
 * Pack the information into a 64-bit value so that each X86_FEATURE_XXX can be
 * passed by value with no overhead.
 */
struct kvm_x86_cpu_feature {
	u32	function;
	u16	index;
	u8	reg;
	u8	bit;
};
#define	KVM_X86_CPU_FEATURE(fn, idx, gpr, __bit)				\
({										\
	struct kvm_x86_cpu_feature feature = {					\
		.function = fn,							\
		.index = idx,							\
		.reg = KVM_CPUID_##gpr,						\
		.bit = __bit,							\
	};									\
										\
	kvm_static_assert((fn & 0xc0000000) == 0 ||				\
			  (fn & 0xc0000000) == 0x40000000 ||			\
			  (fn & 0xc0000000) == 0x80000000 ||			\
			  (fn & 0xc0000000) == 0xc0000000);			\
	kvm_static_assert(idx < BIT(sizeof(feature.index) * BITS_PER_BYTE));	\
	feature;								\
})

/*
 * Basic Leafs, a.k.a. Intel defined
 */
#define	X86_FEATURE_MWAIT		KVM_X86_CPU_FEATURE(0x1, 0, ECX, 3)
#define	X86_FEATURE_VMX			KVM_X86_CPU_FEATURE(0x1, 0, ECX, 5)
#define	X86_FEATURE_SMX			KVM_X86_CPU_FEATURE(0x1, 0, ECX, 6)
#define	X86_FEATURE_PDCM		KVM_X86_CPU_FEATURE(0x1, 0, ECX, 15)
#define	X86_FEATURE_PCID		KVM_X86_CPU_FEATURE(0x1, 0, ECX, 17)
#define X86_FEATURE_X2APIC		KVM_X86_CPU_FEATURE(0x1, 0, ECX, 21)
#define	X86_FEATURE_MOVBE		KVM_X86_CPU_FEATURE(0x1, 0, ECX, 22)
#define	X86_FEATURE_TSC_DEADLINE_TIMER	KVM_X86_CPU_FEATURE(0x1, 0, ECX, 24)
#define	X86_FEATURE_XSAVE		KVM_X86_CPU_FEATURE(0x1, 0, ECX, 26)
#define	X86_FEATURE_OSXSAVE		KVM_X86_CPU_FEATURE(0x1, 0, ECX, 27)
#define	X86_FEATURE_RDRAND		KVM_X86_CPU_FEATURE(0x1, 0, ECX, 30)
#define	X86_FEATURE_HYPERVISOR		KVM_X86_CPU_FEATURE(0x1, 0, ECX, 31)
#define X86_FEATURE_PAE			KVM_X86_CPU_FEATURE(0x1, 0, EDX, 6)
#define	X86_FEATURE_MCE			KVM_X86_CPU_FEATURE(0x1, 0, EDX, 7)
#define	X86_FEATURE_APIC		KVM_X86_CPU_FEATURE(0x1, 0, EDX, 9)
#define	X86_FEATURE_CLFLUSH		KVM_X86_CPU_FEATURE(0x1, 0, EDX, 19)
#define	X86_FEATURE_XMM			KVM_X86_CPU_FEATURE(0x1, 0, EDX, 25)
#define	X86_FEATURE_XMM2		KVM_X86_CPU_FEATURE(0x1, 0, EDX, 26)
#define	X86_FEATURE_FSGSBASE		KVM_X86_CPU_FEATURE(0x7, 0, EBX, 0)
#define	X86_FEATURE_TSC_ADJUST		KVM_X86_CPU_FEATURE(0x7, 0, EBX, 1)
#define	X86_FEATURE_SGX			KVM_X86_CPU_FEATURE(0x7, 0, EBX, 2)
#define	X86_FEATURE_HLE			KVM_X86_CPU_FEATURE(0x7, 0, EBX, 4)
#define	X86_FEATURE_SMEP	        KVM_X86_CPU_FEATURE(0x7, 0, EBX, 7)
#define	X86_FEATURE_INVPCID		KVM_X86_CPU_FEATURE(0x7, 0, EBX, 10)
#define	X86_FEATURE_RTM			KVM_X86_CPU_FEATURE(0x7, 0, EBX, 11)
#define	X86_FEATURE_MPX			KVM_X86_CPU_FEATURE(0x7, 0, EBX, 14)
#define	X86_FEATURE_SMAP		KVM_X86_CPU_FEATURE(0x7, 0, EBX, 20)
#define	X86_FEATURE_PCOMMIT		KVM_X86_CPU_FEATURE(0x7, 0, EBX, 22)
#define	X86_FEATURE_CLFLUSHOPT		KVM_X86_CPU_FEATURE(0x7, 0, EBX, 23)
#define	X86_FEATURE_CLWB		KVM_X86_CPU_FEATURE(0x7, 0, EBX, 24)
#define	X86_FEATURE_UMIP		KVM_X86_CPU_FEATURE(0x7, 0, ECX, 2)
#define	X86_FEATURE_PKU			KVM_X86_CPU_FEATURE(0x7, 0, ECX, 3)
#define	X86_FEATURE_LA57		KVM_X86_CPU_FEATURE(0x7, 0, ECX, 16)
#define	X86_FEATURE_RDPID		KVM_X86_CPU_FEATURE(0x7, 0, ECX, 22)
#define	X86_FEATURE_SGX_LC		KVM_X86_CPU_FEATURE(0x7, 0, ECX, 30)
#define	X86_FEATURE_SHSTK		KVM_X86_CPU_FEATURE(0x7, 0, ECX, 7)
#define	X86_FEATURE_IBT			KVM_X86_CPU_FEATURE(0x7, 0, EDX, 20)
#define	X86_FEATURE_AMX_TILE		KVM_X86_CPU_FEATURE(0x7, 0, EDX, 24)
#define	X86_FEATURE_SPEC_CTRL		KVM_X86_CPU_FEATURE(0x7, 0, EDX, 26)
#define	X86_FEATURE_ARCH_CAPABILITIES	KVM_X86_CPU_FEATURE(0x7, 0, EDX, 29)
#define	X86_FEATURE_PKS			KVM_X86_CPU_FEATURE(0x7, 0, ECX, 31)
#define	X86_FEATURE_XTILECFG		KVM_X86_CPU_FEATURE(0xD, 0, EAX, 17)
#define	X86_FEATURE_XTILEDATA		KVM_X86_CPU_FEATURE(0xD, 0, EAX, 18)
#define	X86_FEATURE_XSAVES		KVM_X86_CPU_FEATURE(0xD, 1, EAX, 3)
#define	X86_FEATURE_XFD			KVM_X86_CPU_FEATURE(0xD, 1, EAX, 4)
#define X86_FEATURE_XTILEDATA_XFD	KVM_X86_CPU_FEATURE(0xD, 18, ECX, 2)

/*
 * Extended Leafs, a.k.a. AMD defined
 */
#define	X86_FEATURE_SVM			KVM_X86_CPU_FEATURE(0x80000001, 0, ECX, 2)
#define	X86_FEATURE_NX			KVM_X86_CPU_FEATURE(0x80000001, 0, EDX, 20)
#define	X86_FEATURE_GBPAGES		KVM_X86_CPU_FEATURE(0x80000001, 0, EDX, 26)
#define	X86_FEATURE_RDTSCP		KVM_X86_CPU_FEATURE(0x80000001, 0, EDX, 27)
#define	X86_FEATURE_LM			KVM_X86_CPU_FEATURE(0x80000001, 0, EDX, 29)
#define	X86_FEATURE_INVTSC		KVM_X86_CPU_FEATURE(0x80000007, 0, EDX, 8)
#define	X86_FEATURE_RDPRU		KVM_X86_CPU_FEATURE(0x80000008, 0, EBX, 4)
#define	X86_FEATURE_AMD_IBPB		KVM_X86_CPU_FEATURE(0x80000008, 0, EBX, 12)
#define	X86_FEATURE_NPT			KVM_X86_CPU_FEATURE(0x8000000A, 0, EDX, 0)
#define	X86_FEATURE_LBRV		KVM_X86_CPU_FEATURE(0x8000000A, 0, EDX, 1)
#define	X86_FEATURE_NRIPS		KVM_X86_CPU_FEATURE(0x8000000A, 0, EDX, 3)
#define X86_FEATURE_TSCRATEMSR          KVM_X86_CPU_FEATURE(0x8000000A, 0, EDX, 4)
#define X86_FEATURE_PAUSEFILTER         KVM_X86_CPU_FEATURE(0x8000000A, 0, EDX, 10)
#define X86_FEATURE_PFTHRESHOLD         KVM_X86_CPU_FEATURE(0x8000000A, 0, EDX, 12)
#define	X86_FEATURE_VGIF		KVM_X86_CPU_FEATURE(0x8000000A, 0, EDX, 16)
#define X86_FEATURE_SEV			KVM_X86_CPU_FEATURE(0x8000001F, 0, EAX, 1)
#define X86_FEATURE_SEV_ES		KVM_X86_CPU_FEATURE(0x8000001F, 0, EAX, 3)

/*
 * KVM defined paravirt features.
 */
#define X86_FEATURE_KVM_CLOCKSOURCE	KVM_X86_CPU_FEATURE(0x40000001, 0, EAX, 0)
#define X86_FEATURE_KVM_NOP_IO_DELAY	KVM_X86_CPU_FEATURE(0x40000001, 0, EAX, 1)
#define X86_FEATURE_KVM_MMU_OP		KVM_X86_CPU_FEATURE(0x40000001, 0, EAX, 2)
#define X86_FEATURE_KVM_CLOCKSOURCE2	KVM_X86_CPU_FEATURE(0x40000001, 0, EAX, 3)
#define X86_FEATURE_KVM_ASYNC_PF	KVM_X86_CPU_FEATURE(0x40000001, 0, EAX, 4)
#define X86_FEATURE_KVM_STEAL_TIME	KVM_X86_CPU_FEATURE(0x40000001, 0, EAX, 5)
#define X86_FEATURE_KVM_PV_EOI		KVM_X86_CPU_FEATURE(0x40000001, 0, EAX, 6)
#define X86_FEATURE_KVM_PV_UNHALT	KVM_X86_CPU_FEATURE(0x40000001, 0, EAX, 7)
/* Bit 8 apparently isn't used?!?! */
#define X86_FEATURE_KVM_PV_TLB_FLUSH	KVM_X86_CPU_FEATURE(0x40000001, 0, EAX, 9)
#define X86_FEATURE_KVM_ASYNC_PF_VMEXIT	KVM_X86_CPU_FEATURE(0x40000001, 0, EAX, 10)
#define X86_FEATURE_KVM_PV_SEND_IPI	KVM_X86_CPU_FEATURE(0x40000001, 0, EAX, 11)
#define X86_FEATURE_KVM_POLL_CONTROL	KVM_X86_CPU_FEATURE(0x40000001, 0, EAX, 12)
#define X86_FEATURE_KVM_PV_SCHED_YIELD	KVM_X86_CPU_FEATURE(0x40000001, 0, EAX, 13)
#define X86_FEATURE_KVM_ASYNC_PF_INT	KVM_X86_CPU_FEATURE(0x40000001, 0, EAX, 14)
#define X86_FEATURE_KVM_MSI_EXT_DEST_ID	KVM_X86_CPU_FEATURE(0x40000001, 0, EAX, 15)
#define X86_FEATURE_KVM_HC_MAP_GPA_RANGE	KVM_X86_CPU_FEATURE(0x40000001, 0, EAX, 16)
#define X86_FEATURE_KVM_MIGRATION_CONTROL	KVM_X86_CPU_FEATURE(0x40000001, 0, EAX, 17)

/*
 * Same idea as X86_FEATURE_XXX, but X86_PROPERTY_XXX retrieves a multi-bit
 * value/property as opposed to a single-bit feature.  Again, pack the info
 * into a 64-bit value to pass by value with no overhead.
 */
struct kvm_x86_cpu_property {
	u32	function;
	u8	index;
	u8	reg;
	u8	lo_bit;
	u8	hi_bit;
};
#define	KVM_X86_CPU_PROPERTY(fn, idx, gpr, low_bit, high_bit)			\
({										\
	struct kvm_x86_cpu_property property = {				\
		.function = fn,							\
		.index = idx,							\
		.reg = KVM_CPUID_##gpr,						\
		.lo_bit = low_bit,						\
		.hi_bit = high_bit,						\
	};									\
										\
	kvm_static_assert(low_bit < high_bit);					\
	kvm_static_assert((fn & 0xc0000000) == 0 ||				\
			  (fn & 0xc0000000) == 0x40000000 ||			\
			  (fn & 0xc0000000) == 0x80000000 ||			\
			  (fn & 0xc0000000) == 0xc0000000);			\
	kvm_static_assert(idx < BIT(sizeof(property.index) * BITS_PER_BYTE));	\
	property;								\
})

#define X86_PROPERTY_MAX_BASIC_LEAF		KVM_X86_CPU_PROPERTY(0, 0, EAX, 0, 31)
#define X86_PROPERTY_PMU_VERSION		KVM_X86_CPU_PROPERTY(0xa, 0, EAX, 0, 7)
#define X86_PROPERTY_PMU_NR_GP_COUNTERS		KVM_X86_CPU_PROPERTY(0xa, 0, EAX, 8, 15)
#define X86_PROPERTY_PMU_GP_COUNTERS_BIT_WIDTH	KVM_X86_CPU_PROPERTY(0xa, 0, EAX, 16, 23)
#define X86_PROPERTY_PMU_EBX_BIT_VECTOR_LENGTH	KVM_X86_CPU_PROPERTY(0xa, 0, EAX, 24, 31)
#define X86_PROPERTY_PMU_EVENTS_MASK		KVM_X86_CPU_PROPERTY(0xa, 0, EBX, 0, 7)
#define X86_PROPERTY_PMU_FIXED_COUNTERS_BITMASK	KVM_X86_CPU_PROPERTY(0xa, 0, ECX, 0, 31)
#define X86_PROPERTY_PMU_NR_FIXED_COUNTERS	KVM_X86_CPU_PROPERTY(0xa, 0, EDX, 0, 4)
#define X86_PROPERTY_PMU_FIXED_COUNTERS_BIT_WIDTH	KVM_X86_CPU_PROPERTY(0xa, 0, EDX, 5, 12)

#define X86_PROPERTY_SUPPORTED_XCR0_LO		KVM_X86_CPU_PROPERTY(0xd,  0, EAX,  0, 31)
#define X86_PROPERTY_XSTATE_MAX_SIZE_XCR0	KVM_X86_CPU_PROPERTY(0xd,  0, EBX,  0, 31)
#define X86_PROPERTY_XSTATE_MAX_SIZE		KVM_X86_CPU_PROPERTY(0xd,  0, ECX,  0, 31)
#define X86_PROPERTY_SUPPORTED_XCR0_HI		KVM_X86_CPU_PROPERTY(0xd,  0, EDX,  0, 31)

#define X86_PROPERTY_XSTATE_TILE_SIZE		KVM_X86_CPU_PROPERTY(0xd, 18, EAX,  0, 31)
#define X86_PROPERTY_XSTATE_TILE_OFFSET		KVM_X86_CPU_PROPERTY(0xd, 18, EBX,  0, 31)
#define X86_PROPERTY_AMX_MAX_PALETTE_TABLES	KVM_X86_CPU_PROPERTY(0x1d, 0, EAX,  0, 31)
#define X86_PROPERTY_AMX_TOTAL_TILE_BYTES	KVM_X86_CPU_PROPERTY(0x1d, 1, EAX,  0, 15)
#define X86_PROPERTY_AMX_BYTES_PER_TILE		KVM_X86_CPU_PROPERTY(0x1d, 1, EAX, 16, 31)
#define X86_PROPERTY_AMX_BYTES_PER_ROW		KVM_X86_CPU_PROPERTY(0x1d, 1, EBX, 0,  15)
#define X86_PROPERTY_AMX_NR_TILE_REGS		KVM_X86_CPU_PROPERTY(0x1d, 1, EBX, 16, 31)
#define X86_PROPERTY_AMX_MAX_ROWS		KVM_X86_CPU_PROPERTY(0x1d, 1, ECX, 0,  15)

#define X86_PROPERTY_MAX_KVM_LEAF		KVM_X86_CPU_PROPERTY(0x40000000, 0, EAX, 0, 31)

#define X86_PROPERTY_MAX_EXT_LEAF		KVM_X86_CPU_PROPERTY(0x80000000, 0, EAX, 0, 31)
#define X86_PROPERTY_MAX_PHY_ADDR		KVM_X86_CPU_PROPERTY(0x80000008, 0, EAX, 0, 7)
#define X86_PROPERTY_MAX_VIRT_ADDR		KVM_X86_CPU_PROPERTY(0x80000008, 0, EAX, 8, 15)
#define X86_PROPERTY_PHYS_ADDR_REDUCTION	KVM_X86_CPU_PROPERTY(0x8000001F, 0, EBX, 6, 11)

#define X86_PROPERTY_MAX_CENTAUR_LEAF		KVM_X86_CPU_PROPERTY(0xC0000000, 0, EAX, 0, 31)

/*
 * Intel's architectural PMU events are bizarre.  They have a "feature" bit
 * that indicates the feature is _not_ supported, and a property that states
 * the length of the bit mask of unsupported features.  A feature is supported
 * if the size of the bit mask is larger than the "unavailable" bit, and said
 * bit is not set.
 *
 * Wrap the "unavailable" feature to simplify checking whether or not a given
 * architectural event is supported.
 */
struct kvm_x86_pmu_feature {
	struct kvm_x86_cpu_feature anti_feature;
};
#define	KVM_X86_PMU_FEATURE(name, __bit)					\
({										\
	struct kvm_x86_pmu_feature feature = {					\
		.anti_feature = KVM_X86_CPU_FEATURE(0xa, 0, EBX, __bit),	\
	};									\
										\
	feature;								\
})

#define X86_PMU_FEATURE_BRANCH_INSNS_RETIRED	KVM_X86_PMU_FEATURE(BRANCH_INSNS_RETIRED, 5)

static inline unsigned int x86_family(unsigned int eax)
{
	unsigned int x86;

	x86 = (eax >> 8) & 0xf;

	if (x86 == 0xf)
		x86 += (eax >> 20) & 0xff;

	return x86;
}

static inline unsigned int x86_model(unsigned int eax)
{
	return ((eax >> 12) & 0xf0) | ((eax >> 4) & 0x0f);
}

/* Page table bitfield declarations */
#define PTE_PRESENT_MASK        BIT_ULL(0)
#define PTE_WRITABLE_MASK       BIT_ULL(1)
#define PTE_USER_MASK           BIT_ULL(2)
#define PTE_ACCESSED_MASK       BIT_ULL(5)
#define PTE_DIRTY_MASK          BIT_ULL(6)
#define PTE_LARGE_MASK          BIT_ULL(7)
#define PTE_GLOBAL_MASK         BIT_ULL(8)
#define PTE_NX_MASK             BIT_ULL(63)

#define PHYSICAL_PAGE_MASK      GENMASK_ULL(51, 12)

#define PAGE_SHIFT		12
#define PAGE_SIZE		(1ULL << PAGE_SHIFT)
#define PAGE_MASK		(~(PAGE_SIZE-1) & PHYSICAL_PAGE_MASK)

#define HUGEPAGE_SHIFT(x)	(PAGE_SHIFT + (((x) - 1) * 9))
#define HUGEPAGE_SIZE(x)	(1UL << HUGEPAGE_SHIFT(x))
#define HUGEPAGE_MASK(x)	(~(HUGEPAGE_SIZE(x) - 1) & PHYSICAL_PAGE_MASK)

#define PTE_GET_PA(pte)		((pte) & PHYSICAL_PAGE_MASK)
#define PTE_GET_PFN(pte)        (PTE_GET_PA(pte) >> PAGE_SHIFT)

/* General Registers in 64-Bit Mode */
struct gpr64_regs {
	u64 rax;
	u64 rcx;
	u64 rdx;
	u64 rbx;
	u64 rsp;
	u64 rbp;
	u64 rsi;
	u64 rdi;
	u64 r8;
	u64 r9;
	u64 r10;
	u64 r11;
	u64 r12;
	u64 r13;
	u64 r14;
	u64 r15;
};

struct desc64 {
	uint16_t limit0;
	uint16_t base0;
	unsigned base1:8, type:4, s:1, dpl:2, p:1;
	unsigned limit1:4, avl:1, l:1, db:1, g:1, base2:8;
	uint32_t base3;
	uint32_t zero1;
} __attribute__((packed));

struct desc_ptr {
	uint16_t size;
	uint64_t address;
} __attribute__((packed));

struct kvm_x86_state {
	struct kvm_xsave *xsave;
	struct kvm_vcpu_events events;
	struct kvm_mp_state mp_state;
	struct kvm_regs regs;
	struct kvm_xcrs xcrs;
	struct kvm_sregs sregs;
	struct kvm_debugregs debugregs;
	union {
		struct kvm_nested_state nested;
		char nested_[16384];
	};
	struct kvm_msrs msrs;
};

static inline uint64_t get_desc64_base(const struct desc64 *desc)
{
	return ((uint64_t)desc->base3 << 32) |
		(desc->base0 | ((desc->base1) << 16) | ((desc->base2) << 24));
}

static inline uint64_t rdtsc(void)
{
	uint32_t eax, edx;
	uint64_t tsc_val;
	/*
	 * The lfence is to wait (on Intel CPUs) until all previous
	 * instructions have been executed. If software requires RDTSC to be
	 * executed prior to execution of any subsequent instruction, it can
	 * execute LFENCE immediately after RDTSC
	 */
	__asm__ __volatile__("lfence; rdtsc; lfence" : "=a"(eax), "=d"(edx));
	tsc_val = ((uint64_t)edx) << 32 | eax;
	return tsc_val;
}

static inline uint64_t rdtscp(uint32_t *aux)
{
	uint32_t eax, edx;

	__asm__ __volatile__("rdtscp" : "=a"(eax), "=d"(edx), "=c"(*aux));
	return ((uint64_t)edx) << 32 | eax;
}

static inline uint64_t rdmsr(uint32_t msr)
{
	uint32_t a, d;

	__asm__ __volatile__("rdmsr" : "=a"(a), "=d"(d) : "c"(msr) : "memory");

	return a | ((uint64_t) d << 32);
}

static inline void wrmsr(uint32_t msr, uint64_t value)
{
	uint32_t a = value;
	uint32_t d = value >> 32;

	__asm__ __volatile__("wrmsr" :: "a"(a), "d"(d), "c"(msr) : "memory");
}


static inline uint16_t inw(uint16_t port)
{
	uint16_t tmp;

	__asm__ __volatile__("in %%dx, %%ax"
		: /* output */ "=a" (tmp)
		: /* input */ "d" (port));

	return tmp;
}

static inline uint16_t get_es(void)
{
	uint16_t es;

	__asm__ __volatile__("mov %%es, %[es]"
			     : /* output */ [es]"=rm"(es));
	return es;
}

static inline uint16_t get_cs(void)
{
	uint16_t cs;

	__asm__ __volatile__("mov %%cs, %[cs]"
			     : /* output */ [cs]"=rm"(cs));
	return cs;
}

static inline uint16_t get_ss(void)
{
	uint16_t ss;

	__asm__ __volatile__("mov %%ss, %[ss]"
			     : /* output */ [ss]"=rm"(ss));
	return ss;
}

static inline uint16_t get_ds(void)
{
	uint16_t ds;

	__asm__ __volatile__("mov %%ds, %[ds]"
			     : /* output */ [ds]"=rm"(ds));
	return ds;
}

static inline uint16_t get_fs(void)
{
	uint16_t fs;

	__asm__ __volatile__("mov %%fs, %[fs]"
			     : /* output */ [fs]"=rm"(fs));
	return fs;
}

static inline uint16_t get_gs(void)
{
	uint16_t gs;

	__asm__ __volatile__("mov %%gs, %[gs]"
			     : /* output */ [gs]"=rm"(gs));
	return gs;
}

static inline uint16_t get_tr(void)
{
	uint16_t tr;

	__asm__ __volatile__("str %[tr]"
			     : /* output */ [tr]"=rm"(tr));
	return tr;
}

static inline uint64_t get_cr0(void)
{
	uint64_t cr0;

	__asm__ __volatile__("mov %%cr0, %[cr0]"
			     : /* output */ [cr0]"=r"(cr0));
	return cr0;
}

static inline uint64_t get_cr3(void)
{
	uint64_t cr3;

	__asm__ __volatile__("mov %%cr3, %[cr3]"
			     : /* output */ [cr3]"=r"(cr3));
	return cr3;
}

static inline uint64_t get_cr4(void)
{
	uint64_t cr4;

	__asm__ __volatile__("mov %%cr4, %[cr4]"
			     : /* output */ [cr4]"=r"(cr4));
	return cr4;
}

static inline void set_cr4(uint64_t val)
{
	__asm__ __volatile__("mov %0, %%cr4" : : "r" (val) : "memory");
}

static inline u64 xgetbv(u32 index)
{
	u32 eax, edx;

	__asm__ __volatile__("xgetbv;"
		     : "=a" (eax), "=d" (edx)
		     : "c" (index));
	return eax | ((u64)edx << 32);
}

static inline void xsetbv(u32 index, u64 value)
{
	u32 eax = value;
	u32 edx = value >> 32;

	__asm__ __volatile__("xsetbv" :: "a" (eax), "d" (edx), "c" (index));
}

static inline struct desc_ptr get_gdt(void)
{
	struct desc_ptr gdt;
	__asm__ __volatile__("sgdt %[gdt]"
			     : /* output */ [gdt]"=m"(gdt));
	return gdt;
}

static inline struct desc_ptr get_idt(void)
{
	struct desc_ptr idt;
	__asm__ __volatile__("sidt %[idt]"
			     : /* output */ [idt]"=m"(idt));
	return idt;
}

static inline void outl(uint16_t port, uint32_t value)
{
	__asm__ __volatile__("outl %%eax, %%dx" : : "d"(port), "a"(value));
}

static inline void __cpuid(uint32_t function, uint32_t index,
			   uint32_t *eax, uint32_t *ebx,
			   uint32_t *ecx, uint32_t *edx)
{
	*eax = function;
	*ecx = index;

	asm volatile("cpuid"
	    : "=a" (*eax),
	      "=b" (*ebx),
	      "=c" (*ecx),
	      "=d" (*edx)
	    : "0" (*eax), "2" (*ecx)
	    : "memory");
}

static inline void cpuid(uint32_t function,
			 uint32_t *eax, uint32_t *ebx,
			 uint32_t *ecx, uint32_t *edx)
{
	return __cpuid(function, 0, eax, ebx, ecx, edx);
}

static inline uint32_t this_cpu_fms(void)
{
	uint32_t eax, ebx, ecx, edx;

	cpuid(1, &eax, &ebx, &ecx, &edx);
	return eax;
}

static inline uint32_t this_cpu_family(void)
{
	return x86_family(this_cpu_fms());
}

static inline uint32_t this_cpu_model(void)
{
	return x86_model(this_cpu_fms());
}

static inline bool this_cpu_vendor_string_is(const char *vendor)
{
	const uint32_t *chunk = (const uint32_t *)vendor;
	uint32_t eax, ebx, ecx, edx;

	cpuid(0, &eax, &ebx, &ecx, &edx);
	return (ebx == chunk[0] && edx == chunk[1] && ecx == chunk[2]);
}

static inline bool this_cpu_is_intel(void)
{
	return this_cpu_vendor_string_is("GenuineIntel");
}

/*
 * Exclude early K5 samples with a vendor string of "AMDisbetter!"
 */
static inline bool this_cpu_is_amd(void)
{
	return this_cpu_vendor_string_is("AuthenticAMD");
}

static inline uint32_t __this_cpu_has(uint32_t function, uint32_t index,
				      uint8_t reg, uint8_t lo, uint8_t hi)
{
	uint32_t gprs[4];

	__cpuid(function, index,
		&gprs[KVM_CPUID_EAX], &gprs[KVM_CPUID_EBX],
		&gprs[KVM_CPUID_ECX], &gprs[KVM_CPUID_EDX]);

	return (gprs[reg] & GENMASK(hi, lo)) >> lo;
}

static inline bool this_cpu_has(struct kvm_x86_cpu_feature feature)
{
	return __this_cpu_has(feature.function, feature.index,
			      feature.reg, feature.bit, feature.bit);
}

static inline uint32_t this_cpu_property(struct kvm_x86_cpu_property property)
{
	return __this_cpu_has(property.function, property.index,
			      property.reg, property.lo_bit, property.hi_bit);
}

static __always_inline bool this_cpu_has_p(struct kvm_x86_cpu_property property)
{
	uint32_t max_leaf;

	switch (property.function & 0xc0000000) {
	case 0:
		max_leaf = this_cpu_property(X86_PROPERTY_MAX_BASIC_LEAF);
		break;
	case 0x40000000:
		max_leaf = this_cpu_property(X86_PROPERTY_MAX_KVM_LEAF);
		break;
	case 0x80000000:
		max_leaf = this_cpu_property(X86_PROPERTY_MAX_EXT_LEAF);
		break;
	case 0xc0000000:
		max_leaf = this_cpu_property(X86_PROPERTY_MAX_CENTAUR_LEAF);
	}
	return max_leaf >= property.function;
}

static inline bool this_pmu_has(struct kvm_x86_pmu_feature feature)
{
	uint32_t nr_bits = this_cpu_property(X86_PROPERTY_PMU_EBX_BIT_VECTOR_LENGTH);

	return nr_bits > feature.anti_feature.bit &&
	       !this_cpu_has(feature.anti_feature);
}

static __always_inline uint64_t this_cpu_supported_xcr0(void)
{
	if (!this_cpu_has_p(X86_PROPERTY_SUPPORTED_XCR0_LO))
		return 0;

	return this_cpu_property(X86_PROPERTY_SUPPORTED_XCR0_LO) |
	       ((uint64_t)this_cpu_property(X86_PROPERTY_SUPPORTED_XCR0_HI) << 32);
}

typedef u32		__attribute__((vector_size(16))) sse128_t;
#define __sse128_u	union { sse128_t vec; u64 as_u64[2]; u32 as_u32[4]; }
#define sse128_lo(x)	({ __sse128_u t; t.vec = x; t.as_u64[0]; })
#define sse128_hi(x)	({ __sse128_u t; t.vec = x; t.as_u64[1]; })

static inline void read_sse_reg(int reg, sse128_t *data)
{
	switch (reg) {
	case 0:
		asm("movdqa %%xmm0, %0" : "=m"(*data));
		break;
	case 1:
		asm("movdqa %%xmm1, %0" : "=m"(*data));
		break;
	case 2:
		asm("movdqa %%xmm2, %0" : "=m"(*data));
		break;
	case 3:
		asm("movdqa %%xmm3, %0" : "=m"(*data));
		break;
	case 4:
		asm("movdqa %%xmm4, %0" : "=m"(*data));
		break;
	case 5:
		asm("movdqa %%xmm5, %0" : "=m"(*data));
		break;
	case 6:
		asm("movdqa %%xmm6, %0" : "=m"(*data));
		break;
	case 7:
		asm("movdqa %%xmm7, %0" : "=m"(*data));
		break;
	default:
		BUG();
	}
}

static inline void write_sse_reg(int reg, const sse128_t *data)
{
	switch (reg) {
	case 0:
		asm("movdqa %0, %%xmm0" : : "m"(*data));
		break;
	case 1:
		asm("movdqa %0, %%xmm1" : : "m"(*data));
		break;
	case 2:
		asm("movdqa %0, %%xmm2" : : "m"(*data));
		break;
	case 3:
		asm("movdqa %0, %%xmm3" : : "m"(*data));
		break;
	case 4:
		asm("movdqa %0, %%xmm4" : : "m"(*data));
		break;
	case 5:
		asm("movdqa %0, %%xmm5" : : "m"(*data));
		break;
	case 6:
		asm("movdqa %0, %%xmm6" : : "m"(*data));
		break;
	case 7:
		asm("movdqa %0, %%xmm7" : : "m"(*data));
		break;
	default:
		BUG();
	}
}

static inline void cpu_relax(void)
{
	asm volatile("rep; nop" ::: "memory");
}

#define ud2()			\
	__asm__ __volatile__(	\
		"ud2\n"	\
		)

#define hlt()			\
	__asm__ __volatile__(	\
		"hlt\n"	\
		)

struct kvm_x86_state *vcpu_save_state(struct kvm_vcpu *vcpu);
void vcpu_load_state(struct kvm_vcpu *vcpu, struct kvm_x86_state *state);
void kvm_x86_state_cleanup(struct kvm_x86_state *state);

const struct kvm_msr_list *kvm_get_msr_index_list(void);
const struct kvm_msr_list *kvm_get_feature_msr_index_list(void);
bool kvm_msr_is_in_save_restore_list(uint32_t msr_index);
uint64_t kvm_get_feature_msr(uint64_t msr_index);

static inline void vcpu_msrs_get(struct kvm_vcpu *vcpu,
				 struct kvm_msrs *msrs)
{
	int r = __vcpu_ioctl(vcpu, KVM_GET_MSRS, msrs);

	TEST_ASSERT(r == msrs->nmsrs,
		    "KVM_GET_MSRS failed, r: %i (failed on MSR %x)",
		    r, r < 0 || r >= msrs->nmsrs ? -1 : msrs->entries[r].index);
}
static inline void vcpu_msrs_set(struct kvm_vcpu *vcpu, struct kvm_msrs *msrs)
{
	int r = __vcpu_ioctl(vcpu, KVM_SET_MSRS, msrs);

	TEST_ASSERT(r == msrs->nmsrs,
		    "KVM_SET_MSRS failed, r: %i (failed on MSR %x)",
		    r, r < 0 || r >= msrs->nmsrs ? -1 : msrs->entries[r].index);
}
static inline void vcpu_debugregs_get(struct kvm_vcpu *vcpu,
				      struct kvm_debugregs *debugregs)
{
	vcpu_ioctl(vcpu, KVM_GET_DEBUGREGS, debugregs);
}
static inline void vcpu_debugregs_set(struct kvm_vcpu *vcpu,
				      struct kvm_debugregs *debugregs)
{
	vcpu_ioctl(vcpu, KVM_SET_DEBUGREGS, debugregs);
}
static inline void vcpu_xsave_get(struct kvm_vcpu *vcpu,
				  struct kvm_xsave *xsave)
{
	vcpu_ioctl(vcpu, KVM_GET_XSAVE, xsave);
}
static inline void vcpu_xsave2_get(struct kvm_vcpu *vcpu,
				   struct kvm_xsave *xsave)
{
	vcpu_ioctl(vcpu, KVM_GET_XSAVE2, xsave);
}
static inline void vcpu_xsave_set(struct kvm_vcpu *vcpu,
				  struct kvm_xsave *xsave)
{
	vcpu_ioctl(vcpu, KVM_SET_XSAVE, xsave);
}
static inline void vcpu_xcrs_get(struct kvm_vcpu *vcpu,
				 struct kvm_xcrs *xcrs)
{
	vcpu_ioctl(vcpu, KVM_GET_XCRS, xcrs);
}
static inline void vcpu_xcrs_set(struct kvm_vcpu *vcpu, struct kvm_xcrs *xcrs)
{
	vcpu_ioctl(vcpu, KVM_SET_XCRS, xcrs);
}

const struct kvm_cpuid_entry2 *get_cpuid_entry(const struct kvm_cpuid2 *cpuid,
					       uint32_t function, uint32_t index);
const struct kvm_cpuid2 *kvm_get_supported_cpuid(void);
const struct kvm_cpuid2 *kvm_get_supported_hv_cpuid(void);
const struct kvm_cpuid2 *vcpu_get_supported_hv_cpuid(struct kvm_vcpu *vcpu);

static inline uint32_t kvm_cpu_fms(void)
{
	return get_cpuid_entry(kvm_get_supported_cpuid(), 0x1, 0)->eax;
}

static inline uint32_t kvm_cpu_family(void)
{
	return x86_family(kvm_cpu_fms());
}

static inline uint32_t kvm_cpu_model(void)
{
	return x86_model(kvm_cpu_fms());
}

bool kvm_cpuid_has(const struct kvm_cpuid2 *cpuid,
		   struct kvm_x86_cpu_feature feature);

static inline bool kvm_cpu_has(struct kvm_x86_cpu_feature feature)
{
	return kvm_cpuid_has(kvm_get_supported_cpuid(), feature);
}

uint32_t kvm_cpuid_property(const struct kvm_cpuid2 *cpuid,
			    struct kvm_x86_cpu_property property);

static inline uint32_t kvm_cpu_property(struct kvm_x86_cpu_property property)
{
	return kvm_cpuid_property(kvm_get_supported_cpuid(), property);
}

static __always_inline bool kvm_cpu_has_p(struct kvm_x86_cpu_property property)
{
	uint32_t max_leaf;

	switch (property.function & 0xc0000000) {
	case 0:
		max_leaf = kvm_cpu_property(X86_PROPERTY_MAX_BASIC_LEAF);
		break;
	case 0x40000000:
		max_leaf = kvm_cpu_property(X86_PROPERTY_MAX_KVM_LEAF);
		break;
	case 0x80000000:
		max_leaf = kvm_cpu_property(X86_PROPERTY_MAX_EXT_LEAF);
		break;
	case 0xc0000000:
		max_leaf = kvm_cpu_property(X86_PROPERTY_MAX_CENTAUR_LEAF);
	}
	return max_leaf >= property.function;
}

static inline bool kvm_pmu_has(struct kvm_x86_pmu_feature feature)
{
	uint32_t nr_bits = kvm_cpu_property(X86_PROPERTY_PMU_EBX_BIT_VECTOR_LENGTH);

	return nr_bits > feature.anti_feature.bit &&
	       !kvm_cpu_has(feature.anti_feature);
}

static inline size_t kvm_cpuid2_size(int nr_entries)
{
	return sizeof(struct kvm_cpuid2) +
	       sizeof(struct kvm_cpuid_entry2) * nr_entries;
}

/*
 * Allocate a "struct kvm_cpuid2* instance, with the 0-length arrary of
 * entries sized to hold @nr_entries.  The caller is responsible for freeing
 * the struct.
 */
static inline struct kvm_cpuid2 *allocate_kvm_cpuid2(int nr_entries)
{
	struct kvm_cpuid2 *cpuid;

	cpuid = malloc(kvm_cpuid2_size(nr_entries));
	TEST_ASSERT(cpuid, "-ENOMEM when allocating kvm_cpuid2");

	cpuid->nent = nr_entries;

	return cpuid;
}

void vcpu_init_cpuid(struct kvm_vcpu *vcpu, const struct kvm_cpuid2 *cpuid);
void vcpu_set_hv_cpuid(struct kvm_vcpu *vcpu);

static inline struct kvm_cpuid_entry2 *__vcpu_get_cpuid_entry(struct kvm_vcpu *vcpu,
							      uint32_t function,
							      uint32_t index)
{
	return (struct kvm_cpuid_entry2 *)get_cpuid_entry(vcpu->cpuid,
							  function, index);
}

static inline struct kvm_cpuid_entry2 *vcpu_get_cpuid_entry(struct kvm_vcpu *vcpu,
							    uint32_t function)
{
	return __vcpu_get_cpuid_entry(vcpu, function, 0);
}

static inline int __vcpu_set_cpuid(struct kvm_vcpu *vcpu)
{
	int r;

	TEST_ASSERT(vcpu->cpuid, "Must do vcpu_init_cpuid() first");
	r = __vcpu_ioctl(vcpu, KVM_SET_CPUID2, vcpu->cpuid);
	if (r)
		return r;

	/* On success, refresh the cache to pick up adjustments made by KVM. */
	vcpu_ioctl(vcpu, KVM_GET_CPUID2, vcpu->cpuid);
	return 0;
}

static inline void vcpu_set_cpuid(struct kvm_vcpu *vcpu)
{
	TEST_ASSERT(vcpu->cpuid, "Must do vcpu_init_cpuid() first");
	vcpu_ioctl(vcpu, KVM_SET_CPUID2, vcpu->cpuid);

	/* Refresh the cache to pick up adjustments made by KVM. */
	vcpu_ioctl(vcpu, KVM_GET_CPUID2, vcpu->cpuid);
}

void vcpu_set_cpuid_maxphyaddr(struct kvm_vcpu *vcpu, uint8_t maxphyaddr);

void vcpu_clear_cpuid_entry(struct kvm_vcpu *vcpu, uint32_t function);
void vcpu_set_or_clear_cpuid_feature(struct kvm_vcpu *vcpu,
				     struct kvm_x86_cpu_feature feature,
				     bool set);

static inline void vcpu_set_cpuid_feature(struct kvm_vcpu *vcpu,
					  struct kvm_x86_cpu_feature feature)
{
	vcpu_set_or_clear_cpuid_feature(vcpu, feature, true);

}

static inline void vcpu_clear_cpuid_feature(struct kvm_vcpu *vcpu,
					    struct kvm_x86_cpu_feature feature)
{
	vcpu_set_or_clear_cpuid_feature(vcpu, feature, false);
}

uint64_t vcpu_get_msr(struct kvm_vcpu *vcpu, uint64_t msr_index);
int _vcpu_set_msr(struct kvm_vcpu *vcpu, uint64_t msr_index, uint64_t msr_value);

/*
 * Assert on an MSR access(es) and pretty print the MSR name when possible.
 * Note, the caller provides the stringified name so that the name of macro is
 * printed, not the value the macro resolves to (due to macro expansion).
 */
#define TEST_ASSERT_MSR(cond, fmt, msr, str, args...)				\
do {										\
	if (__builtin_constant_p(msr)) {					\
		TEST_ASSERT(cond, fmt, str, args);				\
	} else if (!(cond)) {							\
		char buf[16];							\
										\
		snprintf(buf, sizeof(buf), "MSR 0x%x", msr);			\
		TEST_ASSERT(cond, fmt, buf, args);				\
	}									\
} while (0)

/*
 * Returns true if KVM should return the last written value when reading an MSR
 * from userspace, e.g. the MSR isn't a command MSR, doesn't emulate state that
 * is changing, etc.  This is NOT an exhaustive list!  The intent is to filter
 * out MSRs that are not durable _and_ that a selftest wants to write.
 */
static inline bool is_durable_msr(uint32_t msr)
{
	return msr != MSR_IA32_TSC;
}

#define vcpu_set_msr(vcpu, msr, val)							\
do {											\
	uint64_t r, v = val;								\
											\
	TEST_ASSERT_MSR(_vcpu_set_msr(vcpu, msr, v) == 1,				\
			"KVM_SET_MSRS failed on %s, value = 0x%lx", msr, #msr, v);	\
	if (!is_durable_msr(msr))							\
		break;									\
	r = vcpu_get_msr(vcpu, msr);							\
	TEST_ASSERT_MSR(r == v, "Set %s to '0x%lx', got back '0x%lx'", msr, #msr, v, r);\
} while (0)

void kvm_get_cpu_address_width(unsigned int *pa_bits, unsigned int *va_bits);
bool vm_is_unrestricted_guest(struct kvm_vm *vm);

struct ex_regs {
	uint64_t rax, rcx, rdx, rbx;
	uint64_t rbp, rsi, rdi;
	uint64_t r8, r9, r10, r11;
	uint64_t r12, r13, r14, r15;
	uint64_t vector;
	uint64_t error_code;
	uint64_t rip;
	uint64_t cs;
	uint64_t rflags;
};

struct idt_entry {
	uint16_t offset0;
	uint16_t selector;
	uint16_t ist : 3;
	uint16_t : 5;
	uint16_t type : 4;
	uint16_t : 1;
	uint16_t dpl : 2;
	uint16_t p : 1;
	uint16_t offset1;
	uint32_t offset2; uint32_t reserved;
};

void vm_init_descriptor_tables(struct kvm_vm *vm);
void vcpu_init_descriptor_tables(struct kvm_vcpu *vcpu);
void vm_install_exception_handler(struct kvm_vm *vm, int vector,
			void (*handler)(struct ex_regs *));

/* If a toddler were to say "abracadabra". */
#define KVM_EXCEPTION_MAGIC 0xabacadabaULL

/*
 * KVM selftest exception fixup uses registers to coordinate with the exception
 * handler, versus the kernel's in-memory tables and KVM-Unit-Tests's in-memory
 * per-CPU data.  Using only registers avoids having to map memory into the
 * guest, doesn't require a valid, stable GS.base, and reduces the risk of
 * for recursive faults when accessing memory in the handler.  The downside to
 * using registers is that it restricts what registers can be used by the actual
 * instruction.  But, selftests are 64-bit only, making register* pressure a
 * minor concern.  Use r9-r11 as they are volatile, i.e. don't need to be saved
 * by the callee, and except for r11 are not implicit parameters to any
 * instructions.  Ideally, fixup would use r8-r10 and thus avoid implicit
 * parameters entirely, but Hyper-V's hypercall ABI uses r8 and testing Hyper-V
 * is higher priority than testing non-faulting SYSCALL/SYSRET.
 *
 * Note, the fixup handler deliberately does not handle #DE, i.e. the vector
 * is guaranteed to be non-zero on fault.
 *
 * REGISTER INPUTS:
 * r9  = MAGIC
 * r10 = RIP
 * r11 = new RIP on fault
 *
 * REGISTER OUTPUTS:
 * r9  = exception vector (non-zero)
 * r10 = error code
 */
#define KVM_ASM_SAFE(insn)					\
	"mov $" __stringify(KVM_EXCEPTION_MAGIC) ", %%r9\n\t"	\
	"lea 1f(%%rip), %%r10\n\t"				\
	"lea 2f(%%rip), %%r11\n\t"				\
	"1: " insn "\n\t"					\
	"xor %%r9, %%r9\n\t"					\
	"2:\n\t"						\
	"mov  %%r9b, %[vector]\n\t"				\
	"mov  %%r10, %[error_code]\n\t"

#define KVM_ASM_SAFE_OUTPUTS(v, ec)	[vector] "=qm"(v), [error_code] "=rm"(ec)
#define KVM_ASM_SAFE_CLOBBERS	"r9", "r10", "r11"

#define kvm_asm_safe(insn, inputs...)					\
({									\
	uint64_t ign_error_code;					\
	uint8_t vector;							\
									\
	asm volatile(KVM_ASM_SAFE(insn)					\
		     : KVM_ASM_SAFE_OUTPUTS(vector, ign_error_code)	\
		     : inputs						\
		     : KVM_ASM_SAFE_CLOBBERS);				\
	vector;								\
})

#define kvm_asm_safe_ec(insn, error_code, inputs...)			\
({									\
	uint8_t vector;							\
									\
	asm volatile(KVM_ASM_SAFE(insn)					\
		     : KVM_ASM_SAFE_OUTPUTS(vector, error_code)		\
		     : inputs						\
		     : KVM_ASM_SAFE_CLOBBERS);				\
	vector;								\
})

static inline uint8_t rdmsr_safe(uint32_t msr, uint64_t *val)
{
	uint64_t error_code;
	uint8_t vector;
	uint32_t a, d;

	asm volatile(KVM_ASM_SAFE("rdmsr")
		     : "=a"(a), "=d"(d), KVM_ASM_SAFE_OUTPUTS(vector, error_code)
		     : "c"(msr)
		     : KVM_ASM_SAFE_CLOBBERS);

	*val = (uint64_t)a | ((uint64_t)d << 32);
	return vector;
}

static inline uint8_t wrmsr_safe(uint32_t msr, uint64_t val)
{
	return kvm_asm_safe("wrmsr", "a"(val & -1u), "d"(val >> 32), "c"(msr));
}

static inline uint8_t xsetbv_safe(uint32_t index, uint64_t value)
{
	u32 eax = value;
	u32 edx = value >> 32;

	return kvm_asm_safe("xsetbv", "a" (eax), "d" (edx), "c" (index));
}

bool kvm_is_tdp_enabled(void);

uint64_t *__vm_get_page_table_entry(struct kvm_vm *vm, uint64_t vaddr,
				    int *level);
uint64_t *vm_get_page_table_entry(struct kvm_vm *vm, uint64_t vaddr);

uint64_t kvm_hypercall(uint64_t nr, uint64_t a0, uint64_t a1, uint64_t a2,
		       uint64_t a3);
uint64_t __xen_hypercall(uint64_t nr, uint64_t a0, void *a1);
void xen_hypercall(uint64_t nr, uint64_t a0, void *a1);

void __vm_xsave_require_permission(uint64_t xfeature, const char *name);

#define vm_xsave_require_permission(xfeature)	\
	__vm_xsave_require_permission(xfeature, #xfeature)

enum pg_level {
	PG_LEVEL_NONE,
	PG_LEVEL_4K,
	PG_LEVEL_2M,
	PG_LEVEL_1G,
	PG_LEVEL_512G,
	PG_LEVEL_NUM
};

#define PG_LEVEL_SHIFT(_level) ((_level - 1) * 9 + 12)
#define PG_LEVEL_SIZE(_level) (1ull << PG_LEVEL_SHIFT(_level))

#define PG_SIZE_4K PG_LEVEL_SIZE(PG_LEVEL_4K)
#define PG_SIZE_2M PG_LEVEL_SIZE(PG_LEVEL_2M)
#define PG_SIZE_1G PG_LEVEL_SIZE(PG_LEVEL_1G)

void __virt_pg_map(struct kvm_vm *vm, uint64_t vaddr, uint64_t paddr, int level);
void virt_map_level(struct kvm_vm *vm, uint64_t vaddr, uint64_t paddr,
		    uint64_t nr_bytes, int level);

/*
 * Basic CPU control in CR0
 */
#define X86_CR0_PE          (1UL<<0) /* Protection Enable */
#define X86_CR0_MP          (1UL<<1) /* Monitor Coprocessor */
#define X86_CR0_EM          (1UL<<2) /* Emulation */
#define X86_CR0_TS          (1UL<<3) /* Task Switched */
#define X86_CR0_ET          (1UL<<4) /* Extension Type */
#define X86_CR0_NE          (1UL<<5) /* Numeric Error */
#define X86_CR0_WP          (1UL<<16) /* Write Protect */
#define X86_CR0_AM          (1UL<<18) /* Alignment Mask */
#define X86_CR0_NW          (1UL<<29) /* Not Write-through */
#define X86_CR0_CD          (1UL<<30) /* Cache Disable */
#define X86_CR0_PG          (1UL<<31) /* Paging */

#define PFERR_PRESENT_BIT 0
#define PFERR_WRITE_BIT 1
#define PFERR_USER_BIT 2
#define PFERR_RSVD_BIT 3
#define PFERR_FETCH_BIT 4
#define PFERR_PK_BIT 5
#define PFERR_SGX_BIT 15
#define PFERR_GUEST_FINAL_BIT 32
#define PFERR_GUEST_PAGE_BIT 33
#define PFERR_IMPLICIT_ACCESS_BIT 48

#define PFERR_PRESENT_MASK	BIT(PFERR_PRESENT_BIT)
#define PFERR_WRITE_MASK	BIT(PFERR_WRITE_BIT)
#define PFERR_USER_MASK		BIT(PFERR_USER_BIT)
#define PFERR_RSVD_MASK		BIT(PFERR_RSVD_BIT)
#define PFERR_FETCH_MASK	BIT(PFERR_FETCH_BIT)
#define PFERR_PK_MASK		BIT(PFERR_PK_BIT)
#define PFERR_SGX_MASK		BIT(PFERR_SGX_BIT)
#define PFERR_GUEST_FINAL_MASK	BIT_ULL(PFERR_GUEST_FINAL_BIT)
#define PFERR_GUEST_PAGE_MASK	BIT_ULL(PFERR_GUEST_PAGE_BIT)
#define PFERR_IMPLICIT_ACCESS	BIT_ULL(PFERR_IMPLICIT_ACCESS_BIT)

#endif /* SELFTEST_KVM_PROCESSOR_H */
