// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026, Google LLC.
 *
 * Test that KVM correctly virtualizes the PAT MSR and VMCB g_pat field
 * for nested SVM guests:
 *
 * o With nested NPT disabled:
 *     - L1 and L2 share the same PAT
 *     - The vmcb12.g_pat is ignored
 * o With nested NPT enabled:
 *     - Invalid g_pat in vmcb12 should cause VMEXIT_INVALID
 *     - L2 should see vmcb12.g_pat via RDMSR, not L1's PAT
 *     - L2's writes to PAT should be saved to vmcb12 on exit
 *     - L1's PAT should be restored after #VMEXIT from L2
 *     - State save/restore should preserve both L1's and L2's PAT values
 */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "test_util.h"
#include "kvm_util.h"
#include "processor.h"
#include "svm_util.h"

#define PAT_DEFAULT		0x0007040600070406ULL
#define L1_PAT_VALUE		0x0007040600070404ULL  /* Change PA0 to WT */
#define L2_VMCB12_PAT		0x0606060606060606ULL  /* All WB */
#define L2_PAT_MODIFIED		0x0606060606060604ULL  /* Change PA0 to WT */
#define INVALID_PAT_VALUE	0x0808080808080808ULL  /* 8 is reserved */

bool npt_enabled;
int nr_iterations;

static void l2_guest_code(void)
{
	u64 expected_pat = npt_enabled ? L2_VMCB12_PAT : L1_PAT_VALUE;
	int i;

	for (i = 0; i < nr_iterations; i++) {
		GUEST_ASSERT_EQ(rdmsr(MSR_IA32_CR_PAT), expected_pat);
		GUEST_SYNC(1);
		GUEST_ASSERT_EQ(rdmsr(MSR_IA32_CR_PAT), expected_pat);

		wrmsr(MSR_IA32_CR_PAT, L2_PAT_MODIFIED);
		expected_pat = L2_PAT_MODIFIED;

		GUEST_ASSERT_EQ(rdmsr(MSR_IA32_CR_PAT), L2_PAT_MODIFIED);
		GUEST_SYNC(2);
		GUEST_ASSERT_EQ(rdmsr(MSR_IA32_CR_PAT), L2_PAT_MODIFIED);

		vmmcall();
	}
}

static void l1_guest_code(struct svm_test_data *svm)
{
	struct vmcb *vmcb = svm->vmcb;
	int i;

	wrmsr(MSR_IA32_CR_PAT, L1_PAT_VALUE);
	GUEST_ASSERT_EQ(rdmsr(MSR_IA32_CR_PAT), L1_PAT_VALUE);

	generic_svm_setup(svm, l2_guest_code);

	vmcb->save.g_pat = L2_VMCB12_PAT;
	vmcb->control.intercept &= ~(1ULL << INTERCEPT_MSR_PROT);

	for (i = 0; i < nr_iterations; i++) {
		run_guest(vmcb, svm->vmcb_gpa);

		GUEST_ASSERT_EQ(vmcb->control.exit_code, SVM_EXIT_VMMCALL);

		/*
		 * If NPT is enabled by L1, L2 has a unique PAT and L1's PAT is
		 * unchanged. Otherwise, PAT is shared between L1 and L2.
		 */
		if (npt_enabled) {
			GUEST_ASSERT_EQ(vmcb->save.g_pat, L2_PAT_MODIFIED);
			GUEST_ASSERT_EQ(rdmsr(MSR_IA32_CR_PAT), L1_PAT_VALUE);
		} else {
			GUEST_ASSERT_EQ(rdmsr(MSR_IA32_CR_PAT), L2_PAT_MODIFIED);
		}
		vmcb->save.rip += 3; /* skip over VMMCALL */
	}

	GUEST_DONE();
}

static void l1_guest_code_invalid_gpat(struct svm_test_data *svm)
{
	struct vmcb *vmcb = svm->vmcb;

	/* VMRUN should fail without running L2 */
	generic_svm_setup(svm, NULL);

	vmcb->save.g_pat = INVALID_PAT_VALUE;
	run_guest(vmcb, svm->vmcb_gpa);

	GUEST_ASSERT_EQ(vmcb->control.exit_code, SVM_EXIT_ERR);
	GUEST_DONE();
}

static void run_test(void *guest_code, bool do_save_restore, int nr_iters)
{
	struct kvm_x86_state *state;
	struct kvm_vcpu *vcpu;
	struct kvm_vm *vm;
	struct ucall uc;
	gva_t svm_gva;

	vm = vm_create_with_one_vcpu(&vcpu, guest_code);
	vm_enable_cap(vm, KVM_CAP_DISABLE_QUIRKS2,
		      KVM_X86_QUIRK_NESTED_SVM_SHARED_PAT);

	if (npt_enabled)
		vm_enable_npt(vm);

	vcpu_alloc_svm(vm, &svm_gva);

	if (npt_enabled)
		tdp_identity_map_default_memslots(vm);

	vcpu_args_set(vcpu, 1, svm_gva);

	nr_iterations = nr_iters;
	sync_global_to_guest(vm, npt_enabled);
	sync_global_to_guest(vm, nr_iterations);

	for (;;) {
		vcpu_run(vcpu);
		TEST_ASSERT_KVM_EXIT_REASON(vcpu, KVM_EXIT_IO);

		switch (get_ucall(vcpu, &uc)) {
		case UCALL_ABORT:
			REPORT_GUEST_ASSERT(uc);
			/* NOT REACHED */
		case UCALL_SYNC:
			if (do_save_restore) {
				state = vcpu_save_state(vcpu);
				kvm_vm_release(vm);
				vcpu = vm_recreate_with_one_vcpu(vm);
				vm_enable_cap(vm, KVM_CAP_DISABLE_QUIRKS2,
					      KVM_X86_QUIRK_NESTED_SVM_SHARED_PAT);
				vcpu_load_state(vcpu, state);
				kvm_x86_state_cleanup(state);
			}
			break;
		case UCALL_DONE:
			kvm_vm_free(vm);
			return;
		default:
			TEST_FAIL("Unknown ucall %lu", uc.cmd);
		}
	}
}

#define gpat_test(test_name, guest_code, npt_setting)			\
do {									\
	npt_setting;							\
									\
	if (npt_enabled && !kvm_cpu_has(X86_FEATURE_NPT)) {		\
		pr_info("Skipping: " test_name " (no NPT support)\n");	\
		break;							\
	}								\
									\
	pr_info("Testing: " test_name "\n");				\
	run_test(guest_code, false, 1);					\
									\
	if (guest_code == l1_guest_code) {				\
		pr_info("Testing: " test_name " Save/Restore\n");	\
		run_test(guest_code, true, 1);				\
									\
		pr_info("Testing: " test_name " Multiple VMRUNs\n");	\
		run_test(guest_code, false, 10);			\
	}								\
} while (0)

int main(int argc, char *argv[])
{
	TEST_REQUIRE(kvm_cpu_has(X86_FEATURE_SVM));
	TEST_REQUIRE(kvm_has_cap(KVM_CAP_NESTED_STATE));
	TEST_REQUIRE(kvm_check_cap(KVM_CAP_DISABLE_QUIRKS2) &
		     KVM_X86_QUIRK_NESTED_SVM_SHARED_PAT);

	gpat_test("Invalid gPAT", l1_guest_code_invalid_gpat, npt_enabled = true);
	gpat_test("Nested NPT enabled", l1_guest_code, npt_enabled = true);
	gpat_test("Nested NPT disabled", l1_guest_code, npt_enabled = false);
	return 0;
}
