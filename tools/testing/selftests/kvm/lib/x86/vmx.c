// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018, Google LLC.
 */

#include <asm/msr-index.h>

#include "test_util.h"
#include "kvm_util.h"
#include "processor.h"
#include "vmx.h"

#define KVM_EPT_PAGE_TABLE_MIN_PADDR 0x1c0000

#define EPTP_MT_SHIFT		0 /* EPTP memtype bits 2:0 */
#define EPTP_PWL_SHIFT		3 /* EPTP page walk length bits 5:3 */
#define EPTP_AD_ENABLED_SHIFT	6 /* EPTP AD enabled bit 6 */

#define EPTP_WB			(X86_MEMTYPE_WB << EPTP_MT_SHIFT)
#define EPTP_PWL_4		(3ULL << EPTP_PWL_SHIFT) /* PWL is (levels - 1) */
#define EPTP_AD_ENABLED		(1ULL << EPTP_AD_ENABLED_SHIFT)

bool enable_evmcs;

struct hv_enlightened_vmcs *current_evmcs;
struct hv_vp_assist_page *current_vp_assist;

int vcpu_enable_evmcs(struct kvm_vcpu *vcpu)
{
	uint16_t evmcs_ver;

	vcpu_enable_cap(vcpu, KVM_CAP_HYPERV_ENLIGHTENED_VMCS,
			(unsigned long)&evmcs_ver);

	/* KVM should return supported EVMCS version range */
	TEST_ASSERT(((evmcs_ver >> 8) >= (evmcs_ver & 0xff)) &&
		    (evmcs_ver & 0xff) > 0,
		    "Incorrect EVMCS version range: %x:%x",
		    evmcs_ver & 0xff, evmcs_ver >> 8);

	return evmcs_ver;
}

void vm_enable_ept(struct kvm_vm *vm)
{
	struct pte_masks pte_masks;

	TEST_ASSERT(kvm_cpu_has_ept(), "KVM doesn't support nested EPT");

	/*
	 * EPTs do not have 'present' or 'user' bits, instead bit 0 is the
	 * 'readable' bit.
	 */
	pte_masks = (struct pte_masks) {
		.present	=	0,
		.user		=	0,
		.readable	=	BIT_ULL(0),
		.writable	=	BIT_ULL(1),
		.executable	=	BIT_ULL(2),
		.huge		=	BIT_ULL(7),
		.accessed	=	BIT_ULL(8),
		.dirty		=	BIT_ULL(9),
		.nx		=	0,
	};

	/* TODO: Add support for 5-level EPT. */
	tdp_mmu_init(vm, 4, &pte_masks);
}

/* Allocate memory regions for nested VMX tests.
 *
 * Input Args:
 *   vm - The VM to allocate guest-virtual addresses in.
 *
 * Output Args:
 *   p_vmx_gva - The guest virtual address for the struct vmx_pages.
 *
 * Return:
 *   Pointer to structure with the addresses of the VMX areas.
 */
struct vmx_pages *
vcpu_alloc_vmx(struct kvm_vm *vm, vm_vaddr_t *p_vmx_gva)
{
	vm_vaddr_t vmx_gva = vm_vaddr_alloc_page(vm);
	struct vmx_pages *vmx = addr_gva2hva(vm, vmx_gva);

	/* Setup of a region of guest memory for the vmxon region. */
	vmx->vmxon = (void *)vm_vaddr_alloc_page(vm);
	vmx->vmxon_hva = addr_gva2hva(vm, (uintptr_t)vmx->vmxon);
	vmx->vmxon_gpa = addr_gva2gpa(vm, (uintptr_t)vmx->vmxon);

	/* Setup of a region of guest memory for a vmcs. */
	vmx->vmcs = (void *)vm_vaddr_alloc_page(vm);
	vmx->vmcs_hva = addr_gva2hva(vm, (uintptr_t)vmx->vmcs);
	vmx->vmcs_gpa = addr_gva2gpa(vm, (uintptr_t)vmx->vmcs);

	/* Setup of a region of guest memory for the MSR bitmap. */
	vmx->msr = (void *)vm_vaddr_alloc_page(vm);
	vmx->msr_hva = addr_gva2hva(vm, (uintptr_t)vmx->msr);
	vmx->msr_gpa = addr_gva2gpa(vm, (uintptr_t)vmx->msr);
	memset(vmx->msr_hva, 0, getpagesize());

	/* Setup of a region of guest memory for the shadow VMCS. */
	vmx->shadow_vmcs = (void *)vm_vaddr_alloc_page(vm);
	vmx->shadow_vmcs_hva = addr_gva2hva(vm, (uintptr_t)vmx->shadow_vmcs);
	vmx->shadow_vmcs_gpa = addr_gva2gpa(vm, (uintptr_t)vmx->shadow_vmcs);

	/* Setup of a region of guest memory for the VMREAD and VMWRITE bitmaps. */
	vmx->vmread = (void *)vm_vaddr_alloc_page(vm);
	vmx->vmread_hva = addr_gva2hva(vm, (uintptr_t)vmx->vmread);
	vmx->vmread_gpa = addr_gva2gpa(vm, (uintptr_t)vmx->vmread);
	memset(vmx->vmread_hva, 0, getpagesize());

	vmx->vmwrite = (void *)vm_vaddr_alloc_page(vm);
	vmx->vmwrite_hva = addr_gva2hva(vm, (uintptr_t)vmx->vmwrite);
	vmx->vmwrite_gpa = addr_gva2gpa(vm, (uintptr_t)vmx->vmwrite);
	memset(vmx->vmwrite_hva, 0, getpagesize());

	if (vm->stage2_mmu.pgd_created)
		vmx->eptp_gpa = vm->stage2_mmu.pgd;

	*p_vmx_gva = vmx_gva;
	return vmx;
}

bool prepare_for_vmx_operation(struct vmx_pages *vmx)
{
	uint64_t feature_control;
	uint64_t required;
	unsigned long cr0;
	unsigned long cr4;

	/*
	 * Ensure bits in CR0 and CR4 are valid in VMX operation:
	 * - Bit X is 1 in _FIXED0: bit X is fixed to 1 in CRx.
	 * - Bit X is 0 in _FIXED1: bit X is fixed to 0 in CRx.
	 */
	__asm__ __volatile__("mov %%cr0, %0" : "=r"(cr0) : : "memory");
	cr0 &= rdmsr(MSR_IA32_VMX_CR0_FIXED1);
	cr0 |= rdmsr(MSR_IA32_VMX_CR0_FIXED0);
	__asm__ __volatile__("mov %0, %%cr0" : : "r"(cr0) : "memory");

	__asm__ __volatile__("mov %%cr4, %0" : "=r"(cr4) : : "memory");
	cr4 &= rdmsr(MSR_IA32_VMX_CR4_FIXED1);
	cr4 |= rdmsr(MSR_IA32_VMX_CR4_FIXED0);
	/* Enable VMX operation */
	cr4 |= X86_CR4_VMXE;
	__asm__ __volatile__("mov %0, %%cr4" : : "r"(cr4) : "memory");

	/*
	 * Configure IA32_FEATURE_CONTROL MSR to allow VMXON:
	 *  Bit 0: Lock bit. If clear, VMXON causes a #GP.
	 *  Bit 2: Enables VMXON outside of SMX operation. If clear, VMXON
	 *    outside of SMX causes a #GP.
	 */
	required = FEAT_CTL_VMX_ENABLED_OUTSIDE_SMX;
	required |= FEAT_CTL_LOCKED;
	feature_control = rdmsr(MSR_IA32_FEAT_CTL);
	if ((feature_control & required) != required)
		wrmsr(MSR_IA32_FEAT_CTL, feature_control | required);

	/* Enter VMX root operation. */
	*(uint32_t *)(vmx->vmxon) = vmcs_revision();
	if (vmxon(vmx->vmxon_gpa))
		return false;

	return true;
}

bool load_vmcs(struct vmx_pages *vmx)
{
	/* Load a VMCS. */
	*(uint32_t *)(vmx->vmcs) = vmcs_revision();
	if (vmclear(vmx->vmcs_gpa))
		return false;

	if (vmptrld(vmx->vmcs_gpa))
		return false;

	/* Setup shadow VMCS, do not load it yet. */
	*(uint32_t *)(vmx->shadow_vmcs) = vmcs_revision() | 0x80000000ul;
	if (vmclear(vmx->shadow_vmcs_gpa))
		return false;

	return true;
}

static bool ept_vpid_cap_supported(uint64_t mask)
{
	return rdmsr(MSR_IA32_VMX_EPT_VPID_CAP) & mask;
}

bool ept_1g_pages_supported(void)
{
	return ept_vpid_cap_supported(VMX_EPT_VPID_CAP_1G_PAGES);
}

/*
 * Initialize the control fields to the most basic settings possible.
 */
static inline void init_vmcs_control_fields(struct vmx_pages *vmx)
{
	uint32_t sec_exec_ctl = 0;

	vmwrite(VIRTUAL_PROCESSOR_ID, 0);
	vmwrite(POSTED_INTR_NV, 0);

	vmwrite(PIN_BASED_VM_EXEC_CONTROL, rdmsr(MSR_IA32_VMX_TRUE_PINBASED_CTLS));

	if (vmx->eptp_gpa) {
		uint64_t eptp = vmx->eptp_gpa | EPTP_WB | EPTP_PWL_4;

		TEST_ASSERT((vmx->eptp_gpa & ~PHYSICAL_PAGE_MASK) == 0,
			    "Illegal bits set in vmx->eptp_gpa");

		if (ept_vpid_cap_supported(VMX_EPT_VPID_CAP_AD_BITS))
			eptp |= EPTP_AD_ENABLED;

		vmwrite(EPT_POINTER, eptp);
		sec_exec_ctl |= SECONDARY_EXEC_ENABLE_EPT;
	}

	if (!vmwrite(SECONDARY_VM_EXEC_CONTROL, sec_exec_ctl))
		vmwrite(CPU_BASED_VM_EXEC_CONTROL,
			rdmsr(MSR_IA32_VMX_TRUE_PROCBASED_CTLS) | CPU_BASED_ACTIVATE_SECONDARY_CONTROLS);
	else {
		vmwrite(CPU_BASED_VM_EXEC_CONTROL, rdmsr(MSR_IA32_VMX_TRUE_PROCBASED_CTLS));
		GUEST_ASSERT(!sec_exec_ctl);
	}

	vmwrite(EXCEPTION_BITMAP, 0);
	vmwrite(PAGE_FAULT_ERROR_CODE_MASK, 0);
	vmwrite(PAGE_FAULT_ERROR_CODE_MATCH, -1); /* Never match */
	vmwrite(CR3_TARGET_COUNT, 0);
	vmwrite(VM_EXIT_CONTROLS, rdmsr(MSR_IA32_VMX_EXIT_CTLS) |
		VM_EXIT_HOST_ADDR_SPACE_SIZE);	  /* 64-bit host */
	vmwrite(VM_EXIT_MSR_STORE_COUNT, 0);
	vmwrite(VM_EXIT_MSR_LOAD_COUNT, 0);
	vmwrite(VM_ENTRY_CONTROLS, rdmsr(MSR_IA32_VMX_ENTRY_CTLS) |
		VM_ENTRY_IA32E_MODE);		  /* 64-bit guest */
	vmwrite(VM_ENTRY_MSR_LOAD_COUNT, 0);
	vmwrite(VM_ENTRY_INTR_INFO_FIELD, 0);
	vmwrite(TPR_THRESHOLD, 0);

	vmwrite(CR0_GUEST_HOST_MASK, 0);
	vmwrite(CR4_GUEST_HOST_MASK, 0);
	vmwrite(CR0_READ_SHADOW, get_cr0());
	vmwrite(CR4_READ_SHADOW, get_cr4());

	vmwrite(MSR_BITMAP, vmx->msr_gpa);
	vmwrite(VMREAD_BITMAP, vmx->vmread_gpa);
	vmwrite(VMWRITE_BITMAP, vmx->vmwrite_gpa);
}

/*
 * Initialize the host state fields based on the current host state, with
 * the exception of HOST_RSP and HOST_RIP, which should be set by vmlaunch
 * or vmresume.
 */
static inline void init_vmcs_host_state(void)
{
	uint32_t exit_controls = vmreadz(VM_EXIT_CONTROLS);

	vmwrite(HOST_ES_SELECTOR, get_es());
	vmwrite(HOST_CS_SELECTOR, get_cs());
	vmwrite(HOST_SS_SELECTOR, get_ss());
	vmwrite(HOST_DS_SELECTOR, get_ds());
	vmwrite(HOST_FS_SELECTOR, get_fs());
	vmwrite(HOST_GS_SELECTOR, get_gs());
	vmwrite(HOST_TR_SELECTOR, get_tr());

	if (exit_controls & VM_EXIT_LOAD_IA32_PAT)
		vmwrite(HOST_IA32_PAT, rdmsr(MSR_IA32_CR_PAT));
	if (exit_controls & VM_EXIT_LOAD_IA32_EFER)
		vmwrite(HOST_IA32_EFER, rdmsr(MSR_EFER));
	if (exit_controls & VM_EXIT_LOAD_IA32_PERF_GLOBAL_CTRL)
		vmwrite(HOST_IA32_PERF_GLOBAL_CTRL,
			rdmsr(MSR_CORE_PERF_GLOBAL_CTRL));

	vmwrite(HOST_IA32_SYSENTER_CS, rdmsr(MSR_IA32_SYSENTER_CS));

	vmwrite(HOST_CR0, get_cr0());
	vmwrite(HOST_CR3, get_cr3());
	vmwrite(HOST_CR4, get_cr4());
	vmwrite(HOST_FS_BASE, rdmsr(MSR_FS_BASE));
	vmwrite(HOST_GS_BASE, rdmsr(MSR_GS_BASE));
	vmwrite(HOST_TR_BASE,
		get_desc64_base((struct desc64 *)(get_gdt().address + get_tr())));
	vmwrite(HOST_GDTR_BASE, get_gdt().address);
	vmwrite(HOST_IDTR_BASE, get_idt().address);
	vmwrite(HOST_IA32_SYSENTER_ESP, rdmsr(MSR_IA32_SYSENTER_ESP));
	vmwrite(HOST_IA32_SYSENTER_EIP, rdmsr(MSR_IA32_SYSENTER_EIP));
}

/*
 * Initialize the guest state fields essentially as a clone of
 * the host state fields. Some host state fields have fixed
 * values, and we set the corresponding guest state fields accordingly.
 */
static inline void init_vmcs_guest_state(void *rip, void *rsp)
{
	vmwrite(GUEST_ES_SELECTOR, vmreadz(HOST_ES_SELECTOR));
	vmwrite(GUEST_CS_SELECTOR, vmreadz(HOST_CS_SELECTOR));
	vmwrite(GUEST_SS_SELECTOR, vmreadz(HOST_SS_SELECTOR));
	vmwrite(GUEST_DS_SELECTOR, vmreadz(HOST_DS_SELECTOR));
	vmwrite(GUEST_FS_SELECTOR, vmreadz(HOST_FS_SELECTOR));
	vmwrite(GUEST_GS_SELECTOR, vmreadz(HOST_GS_SELECTOR));
	vmwrite(GUEST_LDTR_SELECTOR, 0);
	vmwrite(GUEST_TR_SELECTOR, vmreadz(HOST_TR_SELECTOR));
	vmwrite(GUEST_INTR_STATUS, 0);
	vmwrite(GUEST_PML_INDEX, 0);

	vmwrite(VMCS_LINK_POINTER, -1ll);
	vmwrite(GUEST_IA32_DEBUGCTL, 0);
	vmwrite(GUEST_IA32_PAT, vmreadz(HOST_IA32_PAT));
	vmwrite(GUEST_IA32_EFER, vmreadz(HOST_IA32_EFER));
	vmwrite(GUEST_IA32_PERF_GLOBAL_CTRL,
		vmreadz(HOST_IA32_PERF_GLOBAL_CTRL));

	vmwrite(GUEST_ES_LIMIT, -1);
	vmwrite(GUEST_CS_LIMIT, -1);
	vmwrite(GUEST_SS_LIMIT, -1);
	vmwrite(GUEST_DS_LIMIT, -1);
	vmwrite(GUEST_FS_LIMIT, -1);
	vmwrite(GUEST_GS_LIMIT, -1);
	vmwrite(GUEST_LDTR_LIMIT, -1);
	vmwrite(GUEST_TR_LIMIT, 0x67);
	vmwrite(GUEST_GDTR_LIMIT, 0xffff);
	vmwrite(GUEST_IDTR_LIMIT, 0xffff);
	vmwrite(GUEST_ES_AR_BYTES,
		vmreadz(GUEST_ES_SELECTOR) == 0 ? 0x10000 : 0xc093);
	vmwrite(GUEST_CS_AR_BYTES, 0xa09b);
	vmwrite(GUEST_SS_AR_BYTES, 0xc093);
	vmwrite(GUEST_DS_AR_BYTES,
		vmreadz(GUEST_DS_SELECTOR) == 0 ? 0x10000 : 0xc093);
	vmwrite(GUEST_FS_AR_BYTES,
		vmreadz(GUEST_FS_SELECTOR) == 0 ? 0x10000 : 0xc093);
	vmwrite(GUEST_GS_AR_BYTES,
		vmreadz(GUEST_GS_SELECTOR) == 0 ? 0x10000 : 0xc093);
	vmwrite(GUEST_LDTR_AR_BYTES, 0x10000);
	vmwrite(GUEST_TR_AR_BYTES, 0x8b);
	vmwrite(GUEST_INTERRUPTIBILITY_INFO, 0);
	vmwrite(GUEST_ACTIVITY_STATE, 0);
	vmwrite(GUEST_SYSENTER_CS, vmreadz(HOST_IA32_SYSENTER_CS));
	vmwrite(VMX_PREEMPTION_TIMER_VALUE, 0);

	vmwrite(GUEST_CR0, vmreadz(HOST_CR0));
	vmwrite(GUEST_CR3, vmreadz(HOST_CR3));
	vmwrite(GUEST_CR4, vmreadz(HOST_CR4));
	vmwrite(GUEST_ES_BASE, 0);
	vmwrite(GUEST_CS_BASE, 0);
	vmwrite(GUEST_SS_BASE, 0);
	vmwrite(GUEST_DS_BASE, 0);
	vmwrite(GUEST_FS_BASE, vmreadz(HOST_FS_BASE));
	vmwrite(GUEST_GS_BASE, vmreadz(HOST_GS_BASE));
	vmwrite(GUEST_LDTR_BASE, 0);
	vmwrite(GUEST_TR_BASE, vmreadz(HOST_TR_BASE));
	vmwrite(GUEST_GDTR_BASE, vmreadz(HOST_GDTR_BASE));
	vmwrite(GUEST_IDTR_BASE, vmreadz(HOST_IDTR_BASE));
	vmwrite(GUEST_DR7, 0x400);
	vmwrite(GUEST_RSP, (uint64_t)rsp);
	vmwrite(GUEST_RIP, (uint64_t)rip);
	vmwrite(GUEST_RFLAGS, 2);
	vmwrite(GUEST_PENDING_DBG_EXCEPTIONS, 0);
	vmwrite(GUEST_SYSENTER_ESP, vmreadz(HOST_IA32_SYSENTER_ESP));
	vmwrite(GUEST_SYSENTER_EIP, vmreadz(HOST_IA32_SYSENTER_EIP));
}

void prepare_vmcs(struct vmx_pages *vmx, void *guest_rip, void *guest_rsp)
{
	init_vmcs_control_fields(vmx);
	init_vmcs_host_state();
	init_vmcs_guest_state(guest_rip, guest_rsp);
}

bool kvm_cpu_has_ept(void)
{
	uint64_t ctrl;

	if (!kvm_cpu_has(X86_FEATURE_VMX))
		return false;

	ctrl = kvm_get_feature_msr(MSR_IA32_VMX_TRUE_PROCBASED_CTLS) >> 32;
	if (!(ctrl & CPU_BASED_ACTIVATE_SECONDARY_CONTROLS))
		return false;

	ctrl = kvm_get_feature_msr(MSR_IA32_VMX_PROCBASED_CTLS2) >> 32;
	return ctrl & SECONDARY_EXEC_ENABLE_EPT;
}

void prepare_virtualize_apic_accesses(struct vmx_pages *vmx, struct kvm_vm *vm)
{
	vmx->apic_access = (void *)vm_vaddr_alloc_page(vm);
	vmx->apic_access_hva = addr_gva2hva(vm, (uintptr_t)vmx->apic_access);
	vmx->apic_access_gpa = addr_gva2gpa(vm, (uintptr_t)vmx->apic_access);
}
