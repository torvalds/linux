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
#define	KVM_X86_CPU_FEATURE(fn, idx, gpr, __bit)	\
({							\
	struct kvm_x86_cpu_feature feature = {		\
		.function = fn,				\
		.index = idx,				\
		.reg = KVM_CPUID_##gpr,			\
		.bit = __bit,				\
	};						\
							\
	feature;					\
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
#define	X86_FEATURE_MCE			KVM_X86_CPU_FEATURE(0x1, 0, EDX, 7)
#define	X86_FEATURE_APIC		KVM_X86_CPU_FEATURE(0x1, 0, EDX, 9)
#define	X86_FEATURE_CLFLUSH		KVM_X86_CPU_FEATURE(0x1, 0, EDX, 19)
#define	X86_FEATURE_XMM			KVM_X86_CPU_FEATURE(0x1, 0, EDX, 25)
#define	X86_FEATURE_XMM2		KVM_X86_CPU_FEATURE(0x1, 0, EDX, 26)
#define	X86_FEATURE_FSGSBASE		KVM_X86_CPU_FEATURE(0x7, 0, EBX, 0)
#define	X86_FEATURE_TSC_ADJUST		KVM_X86_CPU_FEATURE(0x7, 0, EBX, 1)
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

/*
 * Extended Leafs, a.k.a. AMD defined
 */
#define	X86_FEATURE_SVM			KVM_X86_CPU_FEATURE(0x80000001, 0, ECX, 2)
#define	X86_FEATURE_NX			KVM_X86_CPU_FEATURE(0x80000001, 0, EDX, 20)
#define	X86_FEATURE_GBPAGES		KVM_X86_CPU_FEATURE(0x80000001, 0, EDX, 26)
#define	X86_FEATURE_RDTSCP		KVM_X86_CPU_FEATURE(0x80000001, 0, EDX, 27)
#define	X86_FEATURE_LM			KVM_X86_CPU_FEATURE(0x80000001, 0, EDX, 29)
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

/* Page table bitfield declarations */
#define PTE_PRESENT_MASK        BIT_ULL(0)
#define PTE_WRITABLE_MASK       BIT_ULL(1)
#define PTE_USER_MASK           BIT_ULL(2)
#define PTE_ACCESSED_MASK       BIT_ULL(5)
#define PTE_DIRTY_MASK          BIT_ULL(6)
#define PTE_LARGE_MASK          BIT_ULL(7)
#define PTE_GLOBAL_MASK         BIT_ULL(8)
#define PTE_NX_MASK             BIT_ULL(63)

#define PAGE_SHIFT		12
#define PAGE_SIZE		(1ULL << PAGE_SHIFT)
#define PAGE_MASK		(~(PAGE_SIZE-1))

#define PHYSICAL_PAGE_MASK      GENMASK_ULL(51, 12)
#define PTE_GET_PFN(pte)        (((pte) & PHYSICAL_PAGE_MASK) >> PAGE_SHIFT)

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

static inline bool this_cpu_has(struct kvm_x86_cpu_feature feature)
{
	uint32_t gprs[4];

	__cpuid(feature.function, feature.index,
		&gprs[KVM_CPUID_EAX], &gprs[KVM_CPUID_EBX],
		&gprs[KVM_CPUID_ECX], &gprs[KVM_CPUID_EDX]);

	return gprs[feature.reg] & BIT(feature.bit);
}

#define SET_XMM(__var, __xmm) \
	asm volatile("movq %0, %%"#__xmm : : "r"(__var) : #__xmm)

static inline void set_xmm(int n, unsigned long val)
{
	switch (n) {
	case 0:
		SET_XMM(val, xmm0);
		break;
	case 1:
		SET_XMM(val, xmm1);
		break;
	case 2:
		SET_XMM(val, xmm2);
		break;
	case 3:
		SET_XMM(val, xmm3);
		break;
	case 4:
		SET_XMM(val, xmm4);
		break;
	case 5:
		SET_XMM(val, xmm5);
		break;
	case 6:
		SET_XMM(val, xmm6);
		break;
	case 7:
		SET_XMM(val, xmm7);
		break;
	}
}

#define GET_XMM(__xmm)							\
({									\
	unsigned long __val;						\
	asm volatile("movq %%"#__xmm", %0" : "=r"(__val));		\
	__val;								\
})

static inline unsigned long get_xmm(int n)
{
	assert(n >= 0 && n <= 7);

	switch (n) {
	case 0:
		return GET_XMM(xmm0);
	case 1:
		return GET_XMM(xmm1);
	case 2:
		return GET_XMM(xmm2);
	case 3:
		return GET_XMM(xmm3);
	case 4:
		return GET_XMM(xmm4);
	case 5:
		return GET_XMM(xmm5);
	case 6:
		return GET_XMM(xmm6);
	case 7:
		return GET_XMM(xmm7);
	}

	/* never reached */
	return 0;
}

static inline void cpu_relax(void)
{
	asm volatile("rep; nop" ::: "memory");
}

#define vmmcall()		\
	__asm__ __volatile__(	\
		"vmmcall\n"	\
		)

#define ud2()			\
	__asm__ __volatile__(	\
		"ud2\n"	\
		)

#define hlt()			\
	__asm__ __volatile__(	\
		"hlt\n"	\
		)

bool is_intel_cpu(void);
bool is_amd_cpu(void);

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
		    "KVM_GET_MSRS failed, r: %i (failed on MSR %x)",
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

const struct kvm_cpuid2 *kvm_get_supported_cpuid(void);
const struct kvm_cpuid2 *kvm_get_supported_hv_cpuid(void);
const struct kvm_cpuid2 *vcpu_get_supported_hv_cpuid(struct kvm_vcpu *vcpu);

bool kvm_cpuid_has(const struct kvm_cpuid2 *cpuid,
		   struct kvm_x86_cpu_feature feature);

static inline bool kvm_cpu_has(struct kvm_x86_cpu_feature feature)
{
	return kvm_cpuid_has(kvm_get_supported_cpuid(), feature);
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

const struct kvm_cpuid_entry2 *get_cpuid_entry(const struct kvm_cpuid2 *cpuid,
					       uint32_t function, uint32_t index);
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

static inline const struct kvm_cpuid_entry2 *__kvm_get_supported_cpuid_entry(uint32_t function,
									     uint32_t index)
{
	return get_cpuid_entry(kvm_get_supported_cpuid(), function, index);
}

static inline const struct kvm_cpuid_entry2 *kvm_get_supported_cpuid_entry(uint32_t function)
{
	return __kvm_get_supported_cpuid_entry(function, 0);
}

uint64_t vcpu_get_msr(struct kvm_vcpu *vcpu, uint64_t msr_index);
int _vcpu_set_msr(struct kvm_vcpu *vcpu, uint64_t msr_index, uint64_t msr_value);

static inline void vcpu_set_msr(struct kvm_vcpu *vcpu, uint64_t msr_index,
				uint64_t msr_value)
{
	int r = _vcpu_set_msr(vcpu, msr_index, msr_value);

	TEST_ASSERT(r == 1, KVM_IOCTL_ERROR(KVM_SET_MSRS, r));
}

static inline uint32_t kvm_get_cpuid_max_basic(void)
{
	return kvm_get_supported_cpuid_entry(0)->eax;
}

static inline uint32_t kvm_get_cpuid_max_extended(void)
{
	return kvm_get_supported_cpuid_entry(0x80000000)->eax;
}

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
 * minor concern.  Use r9-r11 as they are volatile, i.e. don't need* to be saved
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
 */
#define KVM_ASM_SAFE(insn)					\
	"mov $" __stringify(KVM_EXCEPTION_MAGIC) ", %%r9\n\t"	\
	"lea 1f(%%rip), %%r10\n\t"				\
	"lea 2f(%%rip), %%r11\n\t"				\
	"1: " insn "\n\t"					\
	"movb $0, %[vector]\n\t"				\
	"jmp 3f\n\t"						\
	"2:\n\t"						\
	"mov  %%r9b, %[vector]\n\t"				\
	"3:\n\t"

#define KVM_ASM_SAFE_OUTPUTS(v)	[vector] "=qm"(v)
#define KVM_ASM_SAFE_CLOBBERS	"r9", "r10", "r11"

#define kvm_asm_safe(insn, inputs...)			\
({							\
	uint8_t vector;					\
							\
	asm volatile(KVM_ASM_SAFE(insn)			\
		     : KVM_ASM_SAFE_OUTPUTS(vector)	\
		     : inputs				\
		     : KVM_ASM_SAFE_CLOBBERS);		\
	vector;						\
})

static inline uint8_t rdmsr_safe(uint32_t msr, uint64_t *val)
{
	uint8_t vector;
	uint32_t a, d;

	asm volatile(KVM_ASM_SAFE("rdmsr")
		     : "=a"(a), "=d"(d), KVM_ASM_SAFE_OUTPUTS(vector)
		     : "c"(msr)
		     : KVM_ASM_SAFE_CLOBBERS);

	*val = (uint64_t)a | ((uint64_t)d << 32);
	return vector;
}

static inline uint8_t wrmsr_safe(uint32_t msr, uint64_t val)
{
	return kvm_asm_safe("wrmsr", "a"(val & -1u), "d"(val >> 32), "c"(msr));
}

bool kvm_is_tdp_enabled(void);

uint64_t vm_get_page_table_entry(struct kvm_vm *vm, struct kvm_vcpu *vcpu,
				 uint64_t vaddr);
void vm_set_page_table_entry(struct kvm_vm *vm, struct kvm_vcpu *vcpu,
			     uint64_t vaddr, uint64_t pte);

uint64_t kvm_hypercall(uint64_t nr, uint64_t a0, uint64_t a1, uint64_t a2,
		       uint64_t a3);

void __vm_xsave_require_permission(int bit, const char *name);

#define vm_xsave_require_permission(perm)	\
	__vm_xsave_require_permission(perm, #perm)

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

#define XSTATE_XTILE_CFG_BIT		17
#define XSTATE_XTILE_DATA_BIT		18

#define XSTATE_XTILE_CFG_MASK		(1ULL << XSTATE_XTILE_CFG_BIT)
#define XSTATE_XTILE_DATA_MASK		(1ULL << XSTATE_XTILE_DATA_BIT)
#define XFEATURE_XTILE_MASK		(XSTATE_XTILE_CFG_MASK | \
					XSTATE_XTILE_DATA_MASK)
#endif /* SELFTEST_KVM_PROCESSOR_H */
