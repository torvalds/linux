// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026, Google LLC.
 */
#include "kvm_util.h"
#include "vmx.h"
#include "svm_util.h"
#include "kselftest.h"

/*
 * Allocate two VMCB pages for testing. Both pages have different GVAs (shared
 * by both L1 and L2) and L1 GPAs. A single L2 GPA is used such that:
 * - L2 GPA == L1 GPA for VMCB0.
 * - L2 GPA is mapped to L1 GPA for VMCB1 using NPT in L1.
 *
 * This allows testing whether the GPA used by VMSAVE/VMLOAD in L2 is
 * interpreted as a direct L1 GPA or translated using NPT as an L2 GPA, depends
 * on which VMCB is accessed.
 */
#define TEST_MEM_SLOT_INDEX		1
#define TEST_MEM_PAGES			2
#define TEST_MEM_BASE			0xc0000000

#define TEST_GUEST_ADDR(idx)		(TEST_MEM_BASE + (idx) * PAGE_SIZE)

#define TEST_VMCB_L1_GPA(idx)		TEST_GUEST_ADDR(idx)
#define TEST_VMCB_GVA(idx)		TEST_GUEST_ADDR(idx)

#define TEST_VMCB_L2_GPA		TEST_VMCB_L1_GPA(0)

#define L2_GUEST_STACK_SIZE		64

static void l2_guest_code_vmsave(void)
{
	asm volatile("vmsave %0" : : "a"(TEST_VMCB_L2_GPA) : "memory");
}

static void l2_guest_code_vmload(void)
{
	asm volatile("vmload %0" : : "a"(TEST_VMCB_L2_GPA) : "memory");
}

static void l2_guest_code_vmcb(int vmcb_idx)
{
	wrmsr(MSR_KERNEL_GS_BASE, 0xaaaa);
	l2_guest_code_vmsave();

	/* Verify the VMCB used by VMSAVE and update KERNEL_GS_BASE to 0xbbbb */
	GUEST_SYNC(vmcb_idx);

	l2_guest_code_vmload();
	GUEST_ASSERT_EQ(rdmsr(MSR_KERNEL_GS_BASE), 0xbbbb);

	/* Reset MSR_KERNEL_GS_BASE */
	wrmsr(MSR_KERNEL_GS_BASE, 0);
	l2_guest_code_vmsave();

	vmmcall();
}

static void l2_guest_code_vmcb0(void)
{
	l2_guest_code_vmcb(0);
}

static void l2_guest_code_vmcb1(void)
{
	l2_guest_code_vmcb(1);
}

static void l1_guest_code(struct svm_test_data *svm)
{
	unsigned long l2_guest_stack[L2_GUEST_STACK_SIZE];

	/* Each test case initializes the guest RIP below */
	generic_svm_setup(svm, NULL, &l2_guest_stack[L2_GUEST_STACK_SIZE]);

	/* Set VMSAVE/VMLOAD intercepts and make sure they work with.. */
	svm->vmcb->control.intercept |= (BIT_ULL(INTERCEPT_VMSAVE) |
					 BIT_ULL(INTERCEPT_VMLOAD));

	 /* ..VIRTUAL_VMLOAD_VMSAVE_ENABLE_MASK cleared.. */
	svm->vmcb->control.virt_ext &= ~VIRTUAL_VMLOAD_VMSAVE_ENABLE_MASK;

	svm->vmcb->save.rip = (u64)l2_guest_code_vmsave;
	run_guest(svm->vmcb, svm->vmcb_gpa);
	GUEST_ASSERT_EQ(svm->vmcb->control.exit_code, SVM_EXIT_VMSAVE);

	svm->vmcb->save.rip = (u64)l2_guest_code_vmload;
	run_guest(svm->vmcb, svm->vmcb_gpa);
	GUEST_ASSERT_EQ(svm->vmcb->control.exit_code, SVM_EXIT_VMLOAD);

	/* ..and VIRTUAL_VMLOAD_VMSAVE_ENABLE_MASK set */
	svm->vmcb->control.virt_ext |= VIRTUAL_VMLOAD_VMSAVE_ENABLE_MASK;

	svm->vmcb->save.rip = (u64)l2_guest_code_vmsave;
	run_guest(svm->vmcb, svm->vmcb_gpa);
	GUEST_ASSERT_EQ(svm->vmcb->control.exit_code, SVM_EXIT_VMSAVE);

	svm->vmcb->save.rip = (u64)l2_guest_code_vmload;
	run_guest(svm->vmcb, svm->vmcb_gpa);
	GUEST_ASSERT_EQ(svm->vmcb->control.exit_code, SVM_EXIT_VMLOAD);

	/* Now clear the intercepts to test VMSAVE/VMLOAD behavior */
	svm->vmcb->control.intercept &= ~(BIT_ULL(INTERCEPT_VMSAVE) |
					  BIT_ULL(INTERCEPT_VMLOAD));

	/*
	 * Without VIRTUAL_VMLOAD_VMSAVE_ENABLE_MASK, the GPA will be
	 * interpreted as an L1 GPA, so VMCB0 should be used.
	 */
	svm->vmcb->save.rip = (u64)l2_guest_code_vmcb0;
	svm->vmcb->control.virt_ext &= ~VIRTUAL_VMLOAD_VMSAVE_ENABLE_MASK;
	run_guest(svm->vmcb, svm->vmcb_gpa);
	GUEST_ASSERT_EQ(svm->vmcb->control.exit_code, SVM_EXIT_VMMCALL);

	/*
	 * With VIRTUAL_VMLOAD_VMSAVE_ENABLE_MASK, the GPA will be interpeted as
	 * an L2 GPA, and translated through the NPT to VMCB1.
	 */
	svm->vmcb->save.rip = (u64)l2_guest_code_vmcb1;
	svm->vmcb->control.virt_ext |= VIRTUAL_VMLOAD_VMSAVE_ENABLE_MASK;
	run_guest(svm->vmcb, svm->vmcb_gpa);
	GUEST_ASSERT_EQ(svm->vmcb->control.exit_code, SVM_EXIT_VMMCALL);

	GUEST_DONE();
}

int main(int argc, char *argv[])
{
	vm_vaddr_t nested_gva = 0;
	struct vmcb *test_vmcb[2];
	struct kvm_vcpu *vcpu;
	struct kvm_vm *vm;
	int i;

	TEST_REQUIRE(kvm_cpu_has(X86_FEATURE_SVM));
	TEST_REQUIRE(kvm_cpu_has(X86_FEATURE_NPT));
	TEST_REQUIRE(kvm_cpu_has(X86_FEATURE_V_VMSAVE_VMLOAD));

	vm = vm_create_with_one_vcpu(&vcpu, l1_guest_code);
	vm_enable_tdp(vm);

	vcpu_alloc_svm(vm, &nested_gva);
	vcpu_args_set(vcpu, 1, nested_gva);

	vm_userspace_mem_region_add(vm, VM_MEM_SRC_ANONYMOUS,
				    TEST_MEM_BASE, TEST_MEM_SLOT_INDEX,
				    TEST_MEM_PAGES, 0);

	for (i = 0; i <= 1; i++) {
		virt_map(vm, TEST_VMCB_GVA(i), TEST_VMCB_L1_GPA(i), 1);
		test_vmcb[i] = (struct vmcb *)addr_gva2hva(vm, TEST_VMCB_GVA(i));
	}

	tdp_identity_map_default_memslots(vm);

	/*
	 * L2 GPA == L1_GPA(0), but map it to L1_GPA(1), to allow testing
	 * whether the L2 GPA is interpreted as an L1 GPA or translated through
	 * the NPT.
	 */
	TEST_ASSERT_EQ(TEST_VMCB_L2_GPA, TEST_VMCB_L1_GPA(0));
	tdp_map(vm, TEST_VMCB_L2_GPA, TEST_VMCB_L1_GPA(1), PAGE_SIZE);

	for (;;) {
		struct ucall uc;

		vcpu_run(vcpu);
		TEST_ASSERT_KVM_EXIT_REASON(vcpu, KVM_EXIT_IO);

		switch (get_ucall(vcpu, &uc)) {
		case UCALL_ABORT:
			REPORT_GUEST_ASSERT(uc);
		case UCALL_SYNC:
			i = uc.args[1];
			TEST_ASSERT(i == 0 || i == 1, "Unexpected VMCB idx: %d", i);

			/*
			 * Check that only the expected VMCB has KERNEL_GS_BASE
			 * set to 0xaaaa, and update it to 0xbbbb.
			 */
			TEST_ASSERT_EQ(test_vmcb[i]->save.kernel_gs_base, 0xaaaa);
			TEST_ASSERT_EQ(test_vmcb[1-i]->save.kernel_gs_base, 0);
			test_vmcb[i]->save.kernel_gs_base = 0xbbbb;
			break;
		case UCALL_DONE:
			goto done;
		default:
			TEST_FAIL("Unknown ucall %lu", uc.cmd);
		}
	}

done:
	kvm_vm_free(vm);
	return 0;
}
