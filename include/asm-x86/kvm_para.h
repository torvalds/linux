#ifndef __X86_KVM_PARA_H
#define __X86_KVM_PARA_H

/* This CPUID returns the signature 'KVMKVMKVM' in ebx, ecx, and edx.  It
 * should be used to determine that a VM is running under KVM.
 */
#define KVM_CPUID_SIGNATURE	0x40000000

/* This CPUID returns a feature bitmap in eax.  Before enabling a particular
 * paravirtualization, the appropriate feature bit should be checked.
 */
#define KVM_CPUID_FEATURES	0x40000001
#define KVM_FEATURE_CLOCKSOURCE		0
#define KVM_FEATURE_NOP_IO_DELAY	1
#define KVM_FEATURE_MMU_OP		2

#define MSR_KVM_WALL_CLOCK  0x11
#define MSR_KVM_SYSTEM_TIME 0x12

#define KVM_MAX_MMU_OP_BATCH           32

/* Operations for KVM_HC_MMU_OP */
#define KVM_MMU_OP_WRITE_PTE            1
#define KVM_MMU_OP_FLUSH_TLB	        2
#define KVM_MMU_OP_RELEASE_PT	        3

/* Payload for KVM_HC_MMU_OP */
struct kvm_mmu_op_header {
	__u32 op;
	__u32 pad;
};

struct kvm_mmu_op_write_pte {
	struct kvm_mmu_op_header header;
	__u64 pte_phys;
	__u64 pte_val;
};

struct kvm_mmu_op_flush_tlb {
	struct kvm_mmu_op_header header;
};

struct kvm_mmu_op_release_pt {
	struct kvm_mmu_op_header header;
	__u64 pt_phys;
};

#ifdef __KERNEL__
#include <asm/processor.h>

/* xen binary-compatible interface. See xen headers for details */
struct kvm_vcpu_time_info {
	uint32_t version;
	uint32_t pad0;
	uint64_t tsc_timestamp;
	uint64_t system_time;
	uint32_t tsc_to_system_mul;
	int8_t   tsc_shift;
	int8_t	 pad[3];
} __attribute__((__packed__)); /* 32 bytes */

struct kvm_wall_clock {
	uint32_t wc_version;
	uint32_t wc_sec;
	uint32_t wc_nsec;
} __attribute__((__packed__));


extern void kvmclock_init(void);


/* This instruction is vmcall.  On non-VT architectures, it will generate a
 * trap that we will then rewrite to the appropriate instruction.
 */
#define KVM_HYPERCALL ".byte 0x0f,0x01,0xc1"

/* For KVM hypercalls, a three-byte sequence of either the vmrun or the vmmrun
 * instruction.  The hypervisor may replace it with something else but only the
 * instructions are guaranteed to be supported.
 *
 * Up to four arguments may be passed in rbx, rcx, rdx, and rsi respectively.
 * The hypercall number should be placed in rax and the return value will be
 * placed in rax.  No other registers will be clobbered unless explicited
 * noted by the particular hypercall.
 */

static inline long kvm_hypercall0(unsigned int nr)
{
	long ret;
	asm volatile(KVM_HYPERCALL
		     : "=a"(ret)
		     : "a"(nr));
	return ret;
}

static inline long kvm_hypercall1(unsigned int nr, unsigned long p1)
{
	long ret;
	asm volatile(KVM_HYPERCALL
		     : "=a"(ret)
		     : "a"(nr), "b"(p1));
	return ret;
}

static inline long kvm_hypercall2(unsigned int nr, unsigned long p1,
				  unsigned long p2)
{
	long ret;
	asm volatile(KVM_HYPERCALL
		     : "=a"(ret)
		     : "a"(nr), "b"(p1), "c"(p2));
	return ret;
}

static inline long kvm_hypercall3(unsigned int nr, unsigned long p1,
				  unsigned long p2, unsigned long p3)
{
	long ret;
	asm volatile(KVM_HYPERCALL
		     : "=a"(ret)
		     : "a"(nr), "b"(p1), "c"(p2), "d"(p3));
	return ret;
}

static inline long kvm_hypercall4(unsigned int nr, unsigned long p1,
				  unsigned long p2, unsigned long p3,
				  unsigned long p4)
{
	long ret;
	asm volatile(KVM_HYPERCALL
		     : "=a"(ret)
		     : "a"(nr), "b"(p1), "c"(p2), "d"(p3), "S"(p4));
	return ret;
}

static inline int kvm_para_available(void)
{
	unsigned int eax, ebx, ecx, edx;
	char signature[13];

	cpuid(KVM_CPUID_SIGNATURE, &eax, &ebx, &ecx, &edx);
	memcpy(signature + 0, &ebx, 4);
	memcpy(signature + 4, &ecx, 4);
	memcpy(signature + 8, &edx, 4);
	signature[12] = 0;

	if (strcmp(signature, "KVMKVMKVM") == 0)
		return 1;

	return 0;
}

static inline unsigned int kvm_arch_para_features(void)
{
	return cpuid_eax(KVM_CPUID_FEATURES);
}

#endif

#endif
