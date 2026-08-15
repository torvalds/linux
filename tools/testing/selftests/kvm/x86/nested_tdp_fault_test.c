// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025, Google, Inc.
 */

#include "test_util.h"
#include "kvm_util.h"
#include "processor.h"
#include "svm_util.h"
#include "vmx.h"

#define L2_GUEST_STACK_SIZE 64

enum test_type {
	TEST_FINAL_PAGE_UNMAPPED,	    /* Final data page not present */
	TEST_PT_PAGE_UNMAPPED,		    /* Page table page not present */
	TEST_FINAL_PAGE_WRITE_PROTECTED,    /* Final data page read-only */
	TEST_PT_PAGE_WRITE_PROTECTED,	    /* Page table page read-only */
};

static gva_t l2_test_page;
static void (*l2_entry)(void);

#define TEST_IO_PORT 0x80
#define TEST1_VADDR 0x8000000ULL
#define TEST2_VADDR 0x10000000ULL
#define TEST3_VADDR 0x18000000ULL
#define TEST4_VADDR 0x20000000ULL

/*
 * L2 executes OUTS reading from l2_test_page, triggering a nested page
 * fault on the read access.
 */
static void l2_guest_code_outs(void)
{
	asm volatile("outsb" ::"S"(l2_test_page), "d"(TEST_IO_PORT) : "memory");
	GUEST_FAIL("L2 should not reach here");
}

/*
 * L2 executes INS writing to l2_test_page, triggering a nested page
 * fault on the write access.
 */
static void l2_guest_code_ins(void)
{
	asm volatile("insb" ::"D"(l2_test_page), "d"(TEST_IO_PORT) : "memory");
	GUEST_FAIL("L2 should not reach here");
}

#define GUEST_ASSERT_EXIT_QUAL(ac_eq, ex_eq)		\
	__GUEST_ASSERT((ac_eq) == (ex_eq),		\
		       "Wanted EXIT_QUAL '0x%lx', got '0x%lx'", ex_eq, ac_eq)

static void l1_vmx_code(struct vmx_pages *vmx, u64 expected_fault_gpa,
			u64 test_type)
{
	unsigned long l2_guest_stack[L2_GUEST_STACK_SIZE];
	u64 exit_qual;

	GUEST_ASSERT(vmx->vmcs_gpa);
	GUEST_ASSERT(prepare_for_vmx_operation(vmx));
	GUEST_ASSERT(load_vmcs(vmx));

	prepare_vmcs(vmx, l2_entry, &l2_guest_stack[L2_GUEST_STACK_SIZE]);

	GUEST_ASSERT(!vmlaunch());

	/* Verify we got an EPT violation exit */
	__GUEST_ASSERT(vmreadz(VM_EXIT_REASON) == EXIT_REASON_EPT_VIOLATION,
		       "Expected EPT violation (0x%x), got 0x%lx",
		       EXIT_REASON_EPT_VIOLATION,
		       vmreadz(VM_EXIT_REASON));

	__GUEST_ASSERT(vmreadz(GUEST_PHYSICAL_ADDRESS) == expected_fault_gpa,
		       "Expected guest_physical_address = 0x%lx, got 0x%lx",
		       expected_fault_gpa,
		       vmreadz(GUEST_PHYSICAL_ADDRESS));

	exit_qual = vmreadz(EXIT_QUALIFICATION);

	/*
	 * Note, EPT page table accesses are always read+write, e.g. so that
	 * the CPU can do A/D updates at-will.
	 */
	switch (test_type) {
	case TEST_FINAL_PAGE_UNMAPPED:
		GUEST_ASSERT_EXIT_QUAL(exit_qual, EPT_VIOLATION_ACC_READ |
						  EPT_VIOLATION_GVA_IS_VALID |
						  EPT_VIOLATION_GVA_TRANSLATED);
		break;
	case TEST_PT_PAGE_UNMAPPED:
		GUEST_ASSERT_EXIT_QUAL(exit_qual, EPT_VIOLATION_ACC_READ |
						  EPT_VIOLATION_ACC_WRITE |
						  EPT_VIOLATION_GVA_IS_VALID);
		break;
	case TEST_FINAL_PAGE_WRITE_PROTECTED:
		GUEST_ASSERT_EXIT_QUAL(exit_qual, EPT_VIOLATION_ACC_WRITE |
						  EPT_VIOLATION_PROT_READ |
						  EPT_VIOLATION_PROT_EXEC |
						  EPT_VIOLATION_GVA_IS_VALID |
						  EPT_VIOLATION_GVA_TRANSLATED);
		break;
	case TEST_PT_PAGE_WRITE_PROTECTED:
		GUEST_ASSERT_EXIT_QUAL(exit_qual, EPT_VIOLATION_ACC_READ |
						  EPT_VIOLATION_ACC_WRITE |
						  EPT_VIOLATION_PROT_READ |
						  EPT_VIOLATION_PROT_EXEC |
						  EPT_VIOLATION_GVA_IS_VALID);
		break;
	}

	GUEST_DONE();
}

#define GUEST_ASSERT_NPF_EC(ac_ec, ex_ec)		\
	__GUEST_ASSERT((ac_ec) == (ex_ec),		\
		       "Wanted NPF error code '0x%lx', got '0x%lx'", (u64)(ex_ec), ac_ec)


static void l1_svm_code(struct svm_test_data *svm, u64 expected_fault_gpa,
			 u64 test_type)
{
	unsigned long l2_guest_stack[L2_GUEST_STACK_SIZE];
	struct vmcb *vmcb = svm->vmcb;
	u64 exit_info_1;

	generic_svm_setup(svm, l2_entry,
			  &l2_guest_stack[L2_GUEST_STACK_SIZE]);

	run_guest(vmcb, svm->vmcb_gpa);

	/* Verify we got an NPF exit */
	__GUEST_ASSERT(vmcb->control.exit_code == SVM_EXIT_NPF,
		       "Expected NPF exit (0x%x), got 0x%lx", SVM_EXIT_NPF,
		       vmcb->control.exit_code);

	__GUEST_ASSERT(vmcb->control.exit_info_2 == expected_fault_gpa,
		       "Expected exit_info_2 = 0x%lx, got 0x%lx",
		       expected_fault_gpa,
		       vmcb->control.exit_info_2);

	exit_info_1 = vmcb->control.exit_info_1;

	/*
	 * Note, without GMET enabled, NPT walks are always user accesses.  And
	 * like EPT, page table accesses are always read+write.
	 */
	switch (test_type) {
	case TEST_FINAL_PAGE_UNMAPPED:
		GUEST_ASSERT_NPF_EC(exit_info_1, PFERR_USER_MASK |
						 PFERR_GUEST_FINAL_MASK);
		break;
	case TEST_PT_PAGE_UNMAPPED:
		GUEST_ASSERT_NPF_EC(exit_info_1, PFERR_WRITE_MASK |
						 PFERR_USER_MASK |
						 PFERR_GUEST_PAGE_MASK);
		break;
	case TEST_FINAL_PAGE_WRITE_PROTECTED:
		GUEST_ASSERT_NPF_EC(exit_info_1, PFERR_PRESENT_MASK |
						 PFERR_WRITE_MASK |
						 PFERR_USER_MASK |
						 PFERR_GUEST_FINAL_MASK);
		break;
	case TEST_PT_PAGE_WRITE_PROTECTED:
		GUEST_ASSERT_NPF_EC(exit_info_1, PFERR_PRESENT_MASK |
						 PFERR_WRITE_MASK |
						 PFERR_USER_MASK |
						 PFERR_GUEST_PAGE_MASK);
		break;
	}

	GUEST_DONE();
}

static void l1_guest_code(void *data, u64 expected_fault_gpa,
			  u64 test_type)
{
	if (this_cpu_has(X86_FEATURE_VMX))
		l1_vmx_code(data, expected_fault_gpa, test_type);
	else
		l1_svm_code(data, expected_fault_gpa, test_type);
}

/* Returns the GPA of the PT page that maps @vaddr. */
static u64 get_pt_gpa_for_vaddr(struct kvm_vm *vm, u64 vaddr)
{
	u64 *pte;

	pte = vm_get_pte(vm, vaddr);
	TEST_ASSERT(pte && (*pte & 0x1), "PTE not present for vaddr 0x%lx",
		    (unsigned long)vaddr);

	return addr_hva2gpa(vm, (void *)((u64)pte & ~0xFFFULL));
}

static void run_test(enum test_type type)
{
	gpa_t expected_fault_gpa;
	gva_t nested_gva;

	struct kvm_vcpu *vcpu;
	struct kvm_vm *vm;
	struct ucall uc;

	vm = vm_create_with_one_vcpu(&vcpu, l1_guest_code);
	vm_enable_tdp(vm);

	if (kvm_cpu_has(X86_FEATURE_VMX))
		vcpu_alloc_vmx(vm, &nested_gva);
	else
		vcpu_alloc_svm(vm, &nested_gva);

	switch (type) {
	case TEST_FINAL_PAGE_UNMAPPED:
		/*
		 * Unmap the final data page from NPT/EPT. The guest page
		 * table walk succeeds, but the final GPA->HPA translation
		 * fails. L2 reads from the page via OUTS.
		 */
		l2_entry = l2_guest_code_outs;
		l2_test_page = vm_alloc(vm, vm->page_size, TEST1_VADDR);
		expected_fault_gpa = addr_gva2gpa(vm, l2_test_page);
		break;
	case TEST_PT_PAGE_UNMAPPED:
		/*
		 * Unmap a page table page from NPT/EPT. The hardware page
		 * table walk fails when translating the PT page's GPA
		 * through NPT/EPT. L2 reads from the page via OUTS.
		 */
		l2_entry = l2_guest_code_outs;
		l2_test_page = vm_alloc(vm, vm->page_size, TEST2_VADDR);
		expected_fault_gpa = get_pt_gpa_for_vaddr(vm, l2_test_page);
		break;
	case TEST_FINAL_PAGE_WRITE_PROTECTED:
		/*
		 * Write-protect the final data page in NPT/EPT.  The page
		 * is present and readable, but not writable.  L2 writes to
		 * the page via INS, triggering a protection violation.
		 */
		l2_entry = l2_guest_code_ins;
		l2_test_page = vm_alloc(vm, vm->page_size, TEST3_VADDR);
		expected_fault_gpa = addr_gva2gpa(vm, l2_test_page);
		break;
	case TEST_PT_PAGE_WRITE_PROTECTED:
		/*
		 * Write-protect a page table page in NPT/EPT.  The page is
		 * present and readable, but not writable.  The guest page
		 * table walk needs write access to set A/D bits, so it
		 * triggers a protection violation on the PT page.
		 * L2 reads from the page via OUTS.
		 */
		l2_entry = l2_guest_code_outs;
		l2_test_page = vm_alloc(vm, vm->page_size, TEST4_VADDR);
		expected_fault_gpa = get_pt_gpa_for_vaddr(vm, l2_test_page);
		break;
	}

	tdp_identity_map_default_memslots(vm);

	if (type == TEST_FINAL_PAGE_WRITE_PROTECTED ||
	    type == TEST_PT_PAGE_WRITE_PROTECTED)
		*tdp_get_pte(vm, expected_fault_gpa) &= ~PTE_WRITABLE_MASK(&vm->stage2_mmu);
	else
		*tdp_get_pte(vm, expected_fault_gpa) &= ~(PTE_PRESENT_MASK(&vm->stage2_mmu) |
							   PTE_READABLE_MASK(&vm->stage2_mmu) |
							   PTE_WRITABLE_MASK(&vm->stage2_mmu) |
							   PTE_EXECUTABLE_MASK(&vm->stage2_mmu));

	sync_global_to_guest(vm, l2_entry);
	sync_global_to_guest(vm, l2_test_page);
	vcpu_args_set(vcpu, 3, nested_gva, expected_fault_gpa, (u64)type);

	/*
	 * For the INS-based write test, KVM emulates the instruction and
	 * first reads from the I/O port, which exits to userspace.
	 * Re-enter the guest so emulation can proceed to the memory
	 * write, where the nested page fault is triggered.
	 */
	for (;;) {
		vcpu_run(vcpu);

		if (vcpu->run->exit_reason == KVM_EXIT_IO &&
		    vcpu->run->io.port == TEST_IO_PORT &&
		    vcpu->run->io.direction == KVM_EXIT_IO_IN) {
			continue;
		}
		break;
	}

	switch (get_ucall(vcpu, &uc)) {
	case UCALL_DONE:
		break;
	case UCALL_ABORT:
		REPORT_GUEST_ASSERT(uc);
	default:
		TEST_FAIL("Unexpected exit reason: %d", vcpu->run->exit_reason);
	}

	kvm_vm_free(vm);
}

int main(int argc, char *argv[])
{
	TEST_REQUIRE(kvm_cpu_has(X86_FEATURE_VMX) || kvm_cpu_has(X86_FEATURE_SVM));
	TEST_REQUIRE(kvm_cpu_has_tdp());

	run_test(TEST_FINAL_PAGE_UNMAPPED);
	run_test(TEST_PT_PAGE_UNMAPPED);
	run_test(TEST_FINAL_PAGE_WRITE_PROTECTED);
	run_test(TEST_PT_PAGE_WRITE_PROTECTED);

	return 0;
}
