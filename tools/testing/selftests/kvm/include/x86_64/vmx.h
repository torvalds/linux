/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * tools/testing/selftests/kvm/include/x86_64/vmx.h
 *
 * Copyright (C) 2018, Google LLC.
 */

#ifndef SELFTEST_KVM_VMX_H
#define SELFTEST_KVM_VMX_H

#include <stdint.h>
#include "processor.h"

#define CPUID_VMX_BIT				5

#define CPUID_VMX				(1 << 5)

/*
 * Definitions of Primary Processor-Based VM-Execution Controls.
 */
#define CPU_BASED_INTR_WINDOW_EXITING		0x00000004
#define CPU_BASED_USE_TSC_OFFSETTING		0x00000008
#define CPU_BASED_HLT_EXITING			0x00000080
#define CPU_BASED_INVLPG_EXITING		0x00000200
#define CPU_BASED_MWAIT_EXITING			0x00000400
#define CPU_BASED_RDPMC_EXITING			0x00000800
#define CPU_BASED_RDTSC_EXITING			0x00001000
#define CPU_BASED_CR3_LOAD_EXITING		0x00008000
#define CPU_BASED_CR3_STORE_EXITING		0x00010000
#define CPU_BASED_CR8_LOAD_EXITING		0x00080000
#define CPU_BASED_CR8_STORE_EXITING		0x00100000
#define CPU_BASED_TPR_SHADOW			0x00200000
#define CPU_BASED_NMI_WINDOW_EXITING		0x00400000
#define CPU_BASED_MOV_DR_EXITING		0x00800000
#define CPU_BASED_UNCOND_IO_EXITING		0x01000000
#define CPU_BASED_USE_IO_BITMAPS		0x02000000
#define CPU_BASED_MONITOR_TRAP			0x08000000
#define CPU_BASED_USE_MSR_BITMAPS		0x10000000
#define CPU_BASED_MONITOR_EXITING		0x20000000
#define CPU_BASED_PAUSE_EXITING			0x40000000
#define CPU_BASED_ACTIVATE_SECONDARY_CONTROLS	0x80000000

#define CPU_BASED_ALWAYSON_WITHOUT_TRUE_MSR	0x0401e172

/*
 * Definitions of Secondary Processor-Based VM-Execution Controls.
 */
#define SECONDARY_EXEC_VIRTUALIZE_APIC_ACCESSES 0x00000001
#define SECONDARY_EXEC_ENABLE_EPT		0x00000002
#define SECONDARY_EXEC_DESC			0x00000004
#define SECONDARY_EXEC_RDTSCP			0x00000008
#define SECONDARY_EXEC_VIRTUALIZE_X2APIC_MODE	0x00000010
#define SECONDARY_EXEC_ENABLE_VPID		0x00000020
#define SECONDARY_EXEC_WBINVD_EXITING		0x00000040
#define SECONDARY_EXEC_UNRESTRICTED_GUEST	0x00000080
#define SECONDARY_EXEC_APIC_REGISTER_VIRT	0x00000100
#define SECONDARY_EXEC_VIRTUAL_INTR_DELIVERY	0x00000200
#define SECONDARY_EXEC_PAUSE_LOOP_EXITING	0x00000400
#define SECONDARY_EXEC_RDRAND_EXITING		0x00000800
#define SECONDARY_EXEC_ENABLE_INVPCID		0x00001000
#define SECONDARY_EXEC_ENABLE_VMFUNC		0x00002000
#define SECONDARY_EXEC_SHADOW_VMCS		0x00004000
#define SECONDARY_EXEC_RDSEED_EXITING		0x00010000
#define SECONDARY_EXEC_ENABLE_PML		0x00020000
#define SECONDARY_EPT_VE			0x00040000
#define SECONDARY_ENABLE_XSAV_RESTORE		0x00100000
#define SECONDARY_EXEC_TSC_SCALING		0x02000000

#define PIN_BASED_EXT_INTR_MASK			0x00000001
#define PIN_BASED_NMI_EXITING			0x00000008
#define PIN_BASED_VIRTUAL_NMIS			0x00000020
#define PIN_BASED_VMX_PREEMPTION_TIMER		0x00000040
#define PIN_BASED_POSTED_INTR			0x00000080

#define PIN_BASED_ALWAYSON_WITHOUT_TRUE_MSR	0x00000016

#define VM_EXIT_SAVE_DEBUG_CONTROLS		0x00000004
#define VM_EXIT_HOST_ADDR_SPACE_SIZE		0x00000200
#define VM_EXIT_LOAD_IA32_PERF_GLOBAL_CTRL	0x00001000
#define VM_EXIT_ACK_INTR_ON_EXIT		0x00008000
#define VM_EXIT_SAVE_IA32_PAT			0x00040000
#define VM_EXIT_LOAD_IA32_PAT			0x00080000
#define VM_EXIT_SAVE_IA32_EFER			0x00100000
#define VM_EXIT_LOAD_IA32_EFER			0x00200000
#define VM_EXIT_SAVE_VMX_PREEMPTION_TIMER	0x00400000

#define VM_EXIT_ALWAYSON_WITHOUT_TRUE_MSR	0x00036dff

#define VM_ENTRY_LOAD_DEBUG_CONTROLS		0x00000004
#define VM_ENTRY_IA32E_MODE			0x00000200
#define VM_ENTRY_SMM				0x00000400
#define VM_ENTRY_DEACT_DUAL_MONITOR		0x00000800
#define VM_ENTRY_LOAD_IA32_PERF_GLOBAL_CTRL	0x00002000
#define VM_ENTRY_LOAD_IA32_PAT			0x00004000
#define VM_ENTRY_LOAD_IA32_EFER			0x00008000

#define VM_ENTRY_ALWAYSON_WITHOUT_TRUE_MSR	0x000011ff

#define VMX_MISC_PREEMPTION_TIMER_RATE_MASK	0x0000001f
#define VMX_MISC_SAVE_EFER_LMA			0x00000020

#define EXIT_REASON_FAILED_VMENTRY	0x80000000
#define EXIT_REASON_EXCEPTION_NMI	0
#define EXIT_REASON_EXTERNAL_INTERRUPT	1
#define EXIT_REASON_TRIPLE_FAULT	2
#define EXIT_REASON_INTERRUPT_WINDOW	7
#define EXIT_REASON_NMI_WINDOW		8
#define EXIT_REASON_TASK_SWITCH		9
#define EXIT_REASON_CPUID		10
#define EXIT_REASON_HLT			12
#define EXIT_REASON_INVD		13
#define EXIT_REASON_INVLPG		14
#define EXIT_REASON_RDPMC		15
#define EXIT_REASON_RDTSC		16
#define EXIT_REASON_VMCALL		18
#define EXIT_REASON_VMCLEAR		19
#define EXIT_REASON_VMLAUNCH		20
#define EXIT_REASON_VMPTRLD		21
#define EXIT_REASON_VMPTRST		22
#define EXIT_REASON_VMREAD		23
#define EXIT_REASON_VMRESUME		24
#define EXIT_REASON_VMWRITE		25
#define EXIT_REASON_VMOFF		26
#define EXIT_REASON_VMON		27
#define EXIT_REASON_CR_ACCESS		28
#define EXIT_REASON_DR_ACCESS		29
#define EXIT_REASON_IO_INSTRUCTION	30
#define EXIT_REASON_MSR_READ		31
#define EXIT_REASON_MSR_WRITE		32
#define EXIT_REASON_INVALID_STATE	33
#define EXIT_REASON_MWAIT_INSTRUCTION	36
#define EXIT_REASON_MONITOR_INSTRUCTION 39
#define EXIT_REASON_PAUSE_INSTRUCTION	40
#define EXIT_REASON_MCE_DURING_VMENTRY	41
#define EXIT_REASON_TPR_BELOW_THRESHOLD 43
#define EXIT_REASON_APIC_ACCESS		44
#define EXIT_REASON_EOI_INDUCED		45
#define EXIT_REASON_EPT_VIOLATION	48
#define EXIT_REASON_EPT_MISCONFIG	49
#define EXIT_REASON_INVEPT		50
#define EXIT_REASON_RDTSCP		51
#define EXIT_REASON_PREEMPTION_TIMER	52
#define EXIT_REASON_INVVPID		53
#define EXIT_REASON_WBINVD		54
#define EXIT_REASON_XSETBV		55
#define EXIT_REASON_APIC_WRITE		56
#define EXIT_REASON_INVPCID		58
#define EXIT_REASON_PML_FULL		62
#define EXIT_REASON_XSAVES		63
#define EXIT_REASON_XRSTORS		64
#define LAST_EXIT_REASON		64

enum vmcs_field {
	VIRTUAL_PROCESSOR_ID		= 0x00000000,
	POSTED_INTR_NV			= 0x00000002,
	GUEST_ES_SELECTOR		= 0x00000800,
	GUEST_CS_SELECTOR		= 0x00000802,
	GUEST_SS_SELECTOR		= 0x00000804,
	GUEST_DS_SELECTOR		= 0x00000806,
	GUEST_FS_SELECTOR		= 0x00000808,
	GUEST_GS_SELECTOR		= 0x0000080a,
	GUEST_LDTR_SELECTOR		= 0x0000080c,
	GUEST_TR_SELECTOR		= 0x0000080e,
	GUEST_INTR_STATUS		= 0x00000810,
	GUEST_PML_INDEX			= 0x00000812,
	HOST_ES_SELECTOR		= 0x00000c00,
	HOST_CS_SELECTOR		= 0x00000c02,
	HOST_SS_SELECTOR		= 0x00000c04,
	HOST_DS_SELECTOR		= 0x00000c06,
	HOST_FS_SELECTOR		= 0x00000c08,
	HOST_GS_SELECTOR		= 0x00000c0a,
	HOST_TR_SELECTOR		= 0x00000c0c,
	IO_BITMAP_A			= 0x00002000,
	IO_BITMAP_A_HIGH		= 0x00002001,
	IO_BITMAP_B			= 0x00002002,
	IO_BITMAP_B_HIGH		= 0x00002003,
	MSR_BITMAP			= 0x00002004,
	MSR_BITMAP_HIGH			= 0x00002005,
	VM_EXIT_MSR_STORE_ADDR		= 0x00002006,
	VM_EXIT_MSR_STORE_ADDR_HIGH	= 0x00002007,
	VM_EXIT_MSR_LOAD_ADDR		= 0x00002008,
	VM_EXIT_MSR_LOAD_ADDR_HIGH	= 0x00002009,
	VM_ENTRY_MSR_LOAD_ADDR		= 0x0000200a,
	VM_ENTRY_MSR_LOAD_ADDR_HIGH	= 0x0000200b,
	PML_ADDRESS			= 0x0000200e,
	PML_ADDRESS_HIGH		= 0x0000200f,
	TSC_OFFSET			= 0x00002010,
	TSC_OFFSET_HIGH			= 0x00002011,
	VIRTUAL_APIC_PAGE_ADDR		= 0x00002012,
	VIRTUAL_APIC_PAGE_ADDR_HIGH	= 0x00002013,
	APIC_ACCESS_ADDR		= 0x00002014,
	APIC_ACCESS_ADDR_HIGH		= 0x00002015,
	POSTED_INTR_DESC_ADDR		= 0x00002016,
	POSTED_INTR_DESC_ADDR_HIGH	= 0x00002017,
	EPT_POINTER			= 0x0000201a,
	EPT_POINTER_HIGH		= 0x0000201b,
	EOI_EXIT_BITMAP0		= 0x0000201c,
	EOI_EXIT_BITMAP0_HIGH		= 0x0000201d,
	EOI_EXIT_BITMAP1		= 0x0000201e,
	EOI_EXIT_BITMAP1_HIGH		= 0x0000201f,
	EOI_EXIT_BITMAP2		= 0x00002020,
	EOI_EXIT_BITMAP2_HIGH		= 0x00002021,
	EOI_EXIT_BITMAP3		= 0x00002022,
	EOI_EXIT_BITMAP3_HIGH		= 0x00002023,
	VMREAD_BITMAP			= 0x00002026,
	VMREAD_BITMAP_HIGH		= 0x00002027,
	VMWRITE_BITMAP			= 0x00002028,
	VMWRITE_BITMAP_HIGH		= 0x00002029,
	XSS_EXIT_BITMAP			= 0x0000202C,
	XSS_EXIT_BITMAP_HIGH		= 0x0000202D,
	TSC_MULTIPLIER			= 0x00002032,
	TSC_MULTIPLIER_HIGH		= 0x00002033,
	GUEST_PHYSICAL_ADDRESS		= 0x00002400,
	GUEST_PHYSICAL_ADDRESS_HIGH	= 0x00002401,
	VMCS_LINK_POINTER		= 0x00002800,
	VMCS_LINK_POINTER_HIGH		= 0x00002801,
	GUEST_IA32_DEBUGCTL		= 0x00002802,
	GUEST_IA32_DEBUGCTL_HIGH	= 0x00002803,
	GUEST_IA32_PAT			= 0x00002804,
	GUEST_IA32_PAT_HIGH		= 0x00002805,
	GUEST_IA32_EFER			= 0x00002806,
	GUEST_IA32_EFER_HIGH		= 0x00002807,
	GUEST_IA32_PERF_GLOBAL_CTRL	= 0x00002808,
	GUEST_IA32_PERF_GLOBAL_CTRL_HIGH= 0x00002809,
	GUEST_PDPTR0			= 0x0000280a,
	GUEST_PDPTR0_HIGH		= 0x0000280b,
	GUEST_PDPTR1			= 0x0000280c,
	GUEST_PDPTR1_HIGH		= 0x0000280d,
	GUEST_PDPTR2			= 0x0000280e,
	GUEST_PDPTR2_HIGH		= 0x0000280f,
	GUEST_PDPTR3			= 0x00002810,
	GUEST_PDPTR3_HIGH		= 0x00002811,
	GUEST_BNDCFGS			= 0x00002812,
	GUEST_BNDCFGS_HIGH		= 0x00002813,
	HOST_IA32_PAT			= 0x00002c00,
	HOST_IA32_PAT_HIGH		= 0x00002c01,
	HOST_IA32_EFER			= 0x00002c02,
	HOST_IA32_EFER_HIGH		= 0x00002c03,
	HOST_IA32_PERF_GLOBAL_CTRL	= 0x00002c04,
	HOST_IA32_PERF_GLOBAL_CTRL_HIGH	= 0x00002c05,
	PIN_BASED_VM_EXEC_CONTROL	= 0x00004000,
	CPU_BASED_VM_EXEC_CONTROL	= 0x00004002,
	EXCEPTION_BITMAP		= 0x00004004,
	PAGE_FAULT_ERROR_CODE_MASK	= 0x00004006,
	PAGE_FAULT_ERROR_CODE_MATCH	= 0x00004008,
	CR3_TARGET_COUNT		= 0x0000400a,
	VM_EXIT_CONTROLS		= 0x0000400c,
	VM_EXIT_MSR_STORE_COUNT		= 0x0000400e,
	VM_EXIT_MSR_LOAD_COUNT		= 0x00004010,
	VM_ENTRY_CONTROLS		= 0x00004012,
	VM_ENTRY_MSR_LOAD_COUNT		= 0x00004014,
	VM_ENTRY_INTR_INFO_FIELD	= 0x00004016,
	VM_ENTRY_EXCEPTION_ERROR_CODE	= 0x00004018,
	VM_ENTRY_INSTRUCTION_LEN	= 0x0000401a,
	TPR_THRESHOLD			= 0x0000401c,
	SECONDARY_VM_EXEC_CONTROL	= 0x0000401e,
	PLE_GAP				= 0x00004020,
	PLE_WINDOW			= 0x00004022,
	VM_INSTRUCTION_ERROR		= 0x00004400,
	VM_EXIT_REASON			= 0x00004402,
	VM_EXIT_INTR_INFO		= 0x00004404,
	VM_EXIT_INTR_ERROR_CODE		= 0x00004406,
	IDT_VECTORING_INFO_FIELD	= 0x00004408,
	IDT_VECTORING_ERROR_CODE	= 0x0000440a,
	VM_EXIT_INSTRUCTION_LEN		= 0x0000440c,
	VMX_INSTRUCTION_INFO		= 0x0000440e,
	GUEST_ES_LIMIT			= 0x00004800,
	GUEST_CS_LIMIT			= 0x00004802,
	GUEST_SS_LIMIT			= 0x00004804,
	GUEST_DS_LIMIT			= 0x00004806,
	GUEST_FS_LIMIT			= 0x00004808,
	GUEST_GS_LIMIT			= 0x0000480a,
	GUEST_LDTR_LIMIT		= 0x0000480c,
	GUEST_TR_LIMIT			= 0x0000480e,
	GUEST_GDTR_LIMIT		= 0x00004810,
	GUEST_IDTR_LIMIT		= 0x00004812,
	GUEST_ES_AR_BYTES		= 0x00004814,
	GUEST_CS_AR_BYTES		= 0x00004816,
	GUEST_SS_AR_BYTES		= 0x00004818,
	GUEST_DS_AR_BYTES		= 0x0000481a,
	GUEST_FS_AR_BYTES		= 0x0000481c,
	GUEST_GS_AR_BYTES		= 0x0000481e,
	GUEST_LDTR_AR_BYTES		= 0x00004820,
	GUEST_TR_AR_BYTES		= 0x00004822,
	GUEST_INTERRUPTIBILITY_INFO	= 0x00004824,
	GUEST_ACTIVITY_STATE		= 0X00004826,
	GUEST_SYSENTER_CS		= 0x0000482A,
	VMX_PREEMPTION_TIMER_VALUE	= 0x0000482E,
	HOST_IA32_SYSENTER_CS		= 0x00004c00,
	CR0_GUEST_HOST_MASK		= 0x00006000,
	CR4_GUEST_HOST_MASK		= 0x00006002,
	CR0_READ_SHADOW			= 0x00006004,
	CR4_READ_SHADOW			= 0x00006006,
	CR3_TARGET_VALUE0		= 0x00006008,
	CR3_TARGET_VALUE1		= 0x0000600a,
	CR3_TARGET_VALUE2		= 0x0000600c,
	CR3_TARGET_VALUE3		= 0x0000600e,
	EXIT_QUALIFICATION		= 0x00006400,
	GUEST_LINEAR_ADDRESS		= 0x0000640a,
	GUEST_CR0			= 0x00006800,
	GUEST_CR3			= 0x00006802,
	GUEST_CR4			= 0x00006804,
	GUEST_ES_BASE			= 0x00006806,
	GUEST_CS_BASE			= 0x00006808,
	GUEST_SS_BASE			= 0x0000680a,
	GUEST_DS_BASE			= 0x0000680c,
	GUEST_FS_BASE			= 0x0000680e,
	GUEST_GS_BASE			= 0x00006810,
	GUEST_LDTR_BASE			= 0x00006812,
	GUEST_TR_BASE			= 0x00006814,
	GUEST_GDTR_BASE			= 0x00006816,
	GUEST_IDTR_BASE			= 0x00006818,
	GUEST_DR7			= 0x0000681a,
	GUEST_RSP			= 0x0000681c,
	GUEST_RIP			= 0x0000681e,
	GUEST_RFLAGS			= 0x00006820,
	GUEST_PENDING_DBG_EXCEPTIONS	= 0x00006822,
	GUEST_SYSENTER_ESP		= 0x00006824,
	GUEST_SYSENTER_EIP		= 0x00006826,
	HOST_CR0			= 0x00006c00,
	HOST_CR3			= 0x00006c02,
	HOST_CR4			= 0x00006c04,
	HOST_FS_BASE			= 0x00006c06,
	HOST_GS_BASE			= 0x00006c08,
	HOST_TR_BASE			= 0x00006c0a,
	HOST_GDTR_BASE			= 0x00006c0c,
	HOST_IDTR_BASE			= 0x00006c0e,
	HOST_IA32_SYSENTER_ESP		= 0x00006c10,
	HOST_IA32_SYSENTER_EIP		= 0x00006c12,
	HOST_RSP			= 0x00006c14,
	HOST_RIP			= 0x00006c16,
};

struct vmx_msr_entry {
	uint32_t index;
	uint32_t reserved;
	uint64_t value;
} __attribute__ ((aligned(16)));

#include "evmcs.h"

static inline int vmxon(uint64_t phys)
{
	uint8_t ret;

	__asm__ __volatile__ ("vmxon %[pa]; setna %[ret]"
		: [ret]"=rm"(ret)
		: [pa]"m"(phys)
		: "cc", "memory");

	return ret;
}

static inline void vmxoff(void)
{
	__asm__ __volatile__("vmxoff");
}

static inline int vmclear(uint64_t vmcs_pa)
{
	uint8_t ret;

	__asm__ __volatile__ ("vmclear %[pa]; setna %[ret]"
		: [ret]"=rm"(ret)
		: [pa]"m"(vmcs_pa)
		: "cc", "memory");

	return ret;
}

static inline int vmptrld(uint64_t vmcs_pa)
{
	uint8_t ret;

	if (enable_evmcs)
		return -1;

	__asm__ __volatile__ ("vmptrld %[pa]; setna %[ret]"
		: [ret]"=rm"(ret)
		: [pa]"m"(vmcs_pa)
		: "cc", "memory");

	return ret;
}

static inline int vmptrst(uint64_t *value)
{
	uint64_t tmp;
	uint8_t ret;

	if (enable_evmcs)
		return evmcs_vmptrst(value);

	__asm__ __volatile__("vmptrst %[value]; setna %[ret]"
		: [value]"=m"(tmp), [ret]"=rm"(ret)
		: : "cc", "memory");

	*value = tmp;
	return ret;
}

/*
 * A wrapper around vmptrst that ignores errors and returns zero if the
 * vmptrst instruction fails.
 */
static inline uint64_t vmptrstz(void)
{
	uint64_t value = 0;
	vmptrst(&value);
	return value;
}

/*
 * No guest state (e.g. GPRs) is established by this vmlaunch.
 */
static inline int vmlaunch(void)
{
	int ret;

	if (enable_evmcs)
		return evmcs_vmlaunch();

	__asm__ __volatile__("push %%rbp;"
			     "push %%rcx;"
			     "push %%rdx;"
			     "push %%rsi;"
			     "push %%rdi;"
			     "push $0;"
			     "vmwrite %%rsp, %[host_rsp];"
			     "lea 1f(%%rip), %%rax;"
			     "vmwrite %%rax, %[host_rip];"
			     "vmlaunch;"
			     "incq (%%rsp);"
			     "1: pop %%rax;"
			     "pop %%rdi;"
			     "pop %%rsi;"
			     "pop %%rdx;"
			     "pop %%rcx;"
			     "pop %%rbp;"
			     : [ret]"=&a"(ret)
			     : [host_rsp]"r"((uint64_t)HOST_RSP),
			       [host_rip]"r"((uint64_t)HOST_RIP)
			     : "memory", "cc", "rbx", "r8", "r9", "r10",
			       "r11", "r12", "r13", "r14", "r15");
	return ret;
}

/*
 * No guest state (e.g. GPRs) is established by this vmresume.
 */
static inline int vmresume(void)
{
	int ret;

	if (enable_evmcs)
		return evmcs_vmresume();

	__asm__ __volatile__("push %%rbp;"
			     "push %%rcx;"
			     "push %%rdx;"
			     "push %%rsi;"
			     "push %%rdi;"
			     "push $0;"
			     "vmwrite %%rsp, %[host_rsp];"
			     "lea 1f(%%rip), %%rax;"
			     "vmwrite %%rax, %[host_rip];"
			     "vmresume;"
			     "incq (%%rsp);"
			     "1: pop %%rax;"
			     "pop %%rdi;"
			     "pop %%rsi;"
			     "pop %%rdx;"
			     "pop %%rcx;"
			     "pop %%rbp;"
			     : [ret]"=&a"(ret)
			     : [host_rsp]"r"((uint64_t)HOST_RSP),
			       [host_rip]"r"((uint64_t)HOST_RIP)
			     : "memory", "cc", "rbx", "r8", "r9", "r10",
			       "r11", "r12", "r13", "r14", "r15");
	return ret;
}

static inline void vmcall(void)
{
	/* Currently, L1 destroys our GPRs during vmexits.  */
	__asm__ __volatile__("push %%rbp; vmcall; pop %%rbp" : : :
			     "rax", "rbx", "rcx", "rdx",
			     "rsi", "rdi", "r8", "r9", "r10", "r11", "r12",
			     "r13", "r14", "r15");
}

static inline int vmread(uint64_t encoding, uint64_t *value)
{
	uint64_t tmp;
	uint8_t ret;

	if (enable_evmcs)
		return evmcs_vmread(encoding, value);

	__asm__ __volatile__("vmread %[encoding], %[value]; setna %[ret]"
		: [value]"=rm"(tmp), [ret]"=rm"(ret)
		: [encoding]"r"(encoding)
		: "cc", "memory");

	*value = tmp;
	return ret;
}

/*
 * A wrapper around vmread that ignores errors and returns zero if the
 * vmread instruction fails.
 */
static inline uint64_t vmreadz(uint64_t encoding)
{
	uint64_t value = 0;
	vmread(encoding, &value);
	return value;
}

static inline int vmwrite(uint64_t encoding, uint64_t value)
{
	uint8_t ret;

	if (enable_evmcs)
		return evmcs_vmwrite(encoding, value);

	__asm__ __volatile__ ("vmwrite %[value], %[encoding]; setna %[ret]"
		: [ret]"=rm"(ret)
		: [value]"rm"(value), [encoding]"r"(encoding)
		: "cc", "memory");

	return ret;
}

static inline uint32_t vmcs_revision(void)
{
	return rdmsr(MSR_IA32_VMX_BASIC);
}

struct vmx_pages {
	void *vmxon_hva;
	uint64_t vmxon_gpa;
	void *vmxon;

	void *vmcs_hva;
	uint64_t vmcs_gpa;
	void *vmcs;

	void *msr_hva;
	uint64_t msr_gpa;
	void *msr;

	void *shadow_vmcs_hva;
	uint64_t shadow_vmcs_gpa;
	void *shadow_vmcs;

	void *vmread_hva;
	uint64_t vmread_gpa;
	void *vmread;

	void *vmwrite_hva;
	uint64_t vmwrite_gpa;
	void *vmwrite;

	void *vp_assist_hva;
	uint64_t vp_assist_gpa;
	void *vp_assist;

	void *enlightened_vmcs_hva;
	uint64_t enlightened_vmcs_gpa;
	void *enlightened_vmcs;

	void *eptp_hva;
	uint64_t eptp_gpa;
	void *eptp;
};

struct vmx_pages *vcpu_alloc_vmx(struct kvm_vm *vm, vm_vaddr_t *p_vmx_gva);
bool prepare_for_vmx_operation(struct vmx_pages *vmx);
void prepare_vmcs(struct vmx_pages *vmx, void *guest_rip, void *guest_rsp);
bool load_vmcs(struct vmx_pages *vmx);

void nested_vmx_check_supported(void);

void nested_pg_map(struct vmx_pages *vmx, struct kvm_vm *vm,
		   uint64_t nested_paddr, uint64_t paddr, uint32_t eptp_memslot);
void nested_map(struct vmx_pages *vmx, struct kvm_vm *vm,
		 uint64_t nested_paddr, uint64_t paddr, uint64_t size,
		 uint32_t eptp_memslot);
void nested_map_memslot(struct vmx_pages *vmx, struct kvm_vm *vm,
			uint32_t memslot, uint32_t eptp_memslot);
void prepare_eptp(struct vmx_pages *vmx, struct kvm_vm *vm,
		  uint32_t eptp_memslot);

#endif /* SELFTEST_KVM_VMX_H */
