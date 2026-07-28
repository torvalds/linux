// SPDX-License-Identifier: GPL-2.0-only
/*
 * Helpers used for nested SVM testing
 * Largely inspired from KVM unit test svm.c
 *
 * Copyright (C) 2020, Red Hat, Inc.
 */

#include "test_util.h"
#include "kvm_util.h"
#include "processor.h"
#include "svm_util.h"

#define SEV_DEV_PATH "/dev/sev"

/* Allocate memory regions for nested SVM tests.
 *
 * Input Args:
 *   vm - The VM to allocate guest-virtual addresses in.
 *
 * Output Args:
 *   p_svm_gva - The guest virtual address for the struct svm_test_data.
 *
 * Return:
 *   Pointer to structure with the addresses of the SVM areas.
 */
struct svm_test_data *
vcpu_alloc_svm(struct kvm_vm *vm, gva_t *p_svm_gva)
{
	gva_t svm_gva = vm_alloc_page(vm);
	struct svm_test_data *svm = addr_gva2hva(vm, svm_gva);

	svm->vmcb = (void *)vm_alloc_page(vm);
	svm->vmcb_hva = addr_gva2hva(vm, (uintptr_t)svm->vmcb);
	svm->vmcb_gpa = addr_gva2gpa(vm, (uintptr_t)svm->vmcb);

	svm->save_area = (void *)vm_alloc_page(vm);
	svm->save_area_hva = addr_gva2hva(vm, (uintptr_t)svm->save_area);
	svm->save_area_gpa = addr_gva2gpa(vm, (uintptr_t)svm->save_area);

	svm->msr = (void *)vm_alloc_page(vm);
	svm->msr_hva = addr_gva2hva(vm, (uintptr_t)svm->msr);
	svm->msr_gpa = addr_gva2gpa(vm, (uintptr_t)svm->msr);
	memset(svm->msr_hva, 0, getpagesize());

	svm->stack = (void *)vm_alloc_stack(vm, 1);

	if (vm->stage2_mmu.pgd_created)
		svm->ncr3_gpa = vm->stage2_mmu.pgd;

	*p_svm_gva = svm_gva;
	return svm;
}

static void vmcb_set_seg(struct vmcb_seg *seg, u16 selector,
			 u64 base, u32 limit, u32 attr)
{
	seg->selector = selector;
	seg->attrib = attr;
	seg->limit = limit;
	seg->base = base;
}

void vm_enable_npt(struct kvm_vm *vm)
{
	struct pte_masks pte_masks;

	TEST_ASSERT(kvm_cpu_has_npt(), "KVM doesn't supported nested NPT");

	/*
	 * NPTs use the same PTE format, but deliberately drop the C-bit as the
	 * per-VM shared vs. private information is only meant for stage-1.
	 */
	pte_masks = vm->mmu.arch.pte_masks;
	pte_masks.c = 0;

	/* NPT walks are treated as user accesses, so set the 'user' bit. */
	pte_masks.always_set = pte_masks.user;

	tdp_mmu_init(vm, vm->mmu.pgtable_levels, &pte_masks);
}

void generic_svm_setup(struct svm_test_data *svm, void *guest_rip)
{
	struct vmcb *vmcb = svm->vmcb;
	u64 vmcb_gpa = svm->vmcb_gpa;
	struct vmcb_save_area *save = &vmcb->save;
	struct vmcb_control_area *ctrl = &vmcb->control;
	u32 data_seg_attr = 3 | SVM_SELECTOR_S_MASK | SVM_SELECTOR_P_MASK
	      | SVM_SELECTOR_DB_MASK | SVM_SELECTOR_G_MASK;
	u32 code_seg_attr = 9 | SVM_SELECTOR_S_MASK | SVM_SELECTOR_P_MASK
		| SVM_SELECTOR_L_MASK | SVM_SELECTOR_G_MASK;
	u64 efer;

	efer = rdmsr(MSR_EFER);
	wrmsr(MSR_EFER, efer | EFER_SVME);
	wrmsr(MSR_VM_HSAVE_PA, svm->save_area_gpa);

	memset(vmcb, 0, sizeof(*vmcb));
	asm volatile ("vmsave %0\n\t" : : "a" (vmcb_gpa) : "memory");
	vmcb_set_seg(&save->es, get_es(), 0, -1U, data_seg_attr);
	vmcb_set_seg(&save->cs, get_cs(), 0, -1U, code_seg_attr);
	vmcb_set_seg(&save->ss, get_ss(), 0, -1U, data_seg_attr);
	vmcb_set_seg(&save->ds, get_ds(), 0, -1U, data_seg_attr);
	vmcb_set_seg(&save->gdtr, 0, get_gdt().address, get_gdt().size, 0);
	vmcb_set_seg(&save->idtr, 0, get_idt().address, get_idt().size, 0);

	ctrl->asid = 1;
	save->cpl = 0;
	save->efer = rdmsr(MSR_EFER);
	asm volatile ("mov %%cr4, %0" : "=r"(save->cr4) : : "memory");
	asm volatile ("mov %%cr3, %0" : "=r"(save->cr3) : : "memory");
	asm volatile ("mov %%cr0, %0" : "=r"(save->cr0) : : "memory");
	asm volatile ("mov %%dr7, %0" : "=r"(save->dr7) : : "memory");
	asm volatile ("mov %%dr6, %0" : "=r"(save->dr6) : : "memory");
	asm volatile ("mov %%cr2, %0" : "=r"(save->cr2) : : "memory");
	save->g_pat = rdmsr(MSR_IA32_CR_PAT);
	save->dbgctl = rdmsr(MSR_IA32_DEBUGCTLMSR);
	ctrl->intercept = (1ULL << INTERCEPT_VMRUN) |
				(1ULL << INTERCEPT_VMMCALL);
	ctrl->msrpm_base_pa = svm->msr_gpa;

	vmcb->save.rip = (u64)guest_rip;
	vmcb->save.rsp = (u64)svm->stack;
	guest_regs.rdi = (u64)svm;

	if (svm->ncr3_gpa) {
		ctrl->misc_ctl |= SVM_MISC_ENABLE_NP;
		ctrl->nested_cr3 = svm->ncr3_gpa;
	}
}

/*
 * save/restore 64-bit general registers except rax, rip, rsp
 * which are directly handed through the VMCB guest processor state
 */
#define SVM_SWITCH_GPRS_ASM \
	GUEST_SWITCH_GPR_ASM(rbx) \
	GUEST_SWITCH_GPR_ASM(rcx) \
	GUEST_SWITCH_GPR_ASM(rdx) \
	GUEST_SWITCH_GPR_ASM(rbp) \
	GUEST_SWITCH_GPR_ASM(rsi) \
	GUEST_SWITCH_GPR_ASM(rdi) \
	GUEST_SWITCH_GPR_ASM(r8)  \
	GUEST_SWITCH_GPR_ASM(r9)  \
	GUEST_SWITCH_GPR_ASM(r10) \
	GUEST_SWITCH_GPR_ASM(r11) \
	GUEST_SWITCH_GPR_ASM(r12) \
	GUEST_SWITCH_GPR_ASM(r13) \
	GUEST_SWITCH_GPR_ASM(r14) \
	GUEST_SWITCH_GPR_ASM(r15)

/*
 * selftests do not use interrupts so we dropped clgi/sti/cli/stgi
 * for now. Registers involved in SVM_SWITCH_GPRS_ASM are eventually
 * unmodified so they do not need to be in the clobber list.
 */
void run_guest(struct vmcb *vmcb, u64 vmcb_gpa)
{
	asm volatile (
		"vmload %[vmcb_gpa]\n\t"
		"mov " GUEST_REG(rflags) ", %%r15\n\t"
		"mov %%r15, %[vmcb_rflags]\n\t"
		"mov " GUEST_REG(rax) ", %%r15\n\t"
		"mov %%r15, %[vmcb_rax]\n\t"
		SVM_SWITCH_GPRS_ASM
		"vmrun %[vmcb_gpa]\n\t"
		SVM_SWITCH_GPRS_ASM
		"mov %[vmcb_rflags], %%r15\n\t"
		"mov %%r15, " GUEST_REG(rflags) "\n\t"
		"mov %[vmcb_rax], %%r15\n\t"	// rax
		"mov %%r15, " GUEST_REG(rax) "\n\t"
		"vmsave %[vmcb_gpa]\n\t"
		: [vmcb_rflags] "+m" (vmcb->save.rflags),
		  [vmcb_rax] "+m" (vmcb->save.rax)
		: [vmcb_gpa] "a" (vmcb_gpa),
		  GUEST_REGS_OFFSETS
		: "r15", "memory");
}

/*
 * Open SEV_DEV_PATH if available, otherwise exit the entire program.
 *
 * Return:
 *   The opened file descriptor of /dev/sev.
 */
int open_sev_dev_path_or_exit(void)
{
	return open_path_or_exit(SEV_DEV_PATH, 0);
}
