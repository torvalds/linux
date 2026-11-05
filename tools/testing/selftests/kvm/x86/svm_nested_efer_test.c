// SPDX-License-Identifier: GPL-2.0-only
/*
 * Tests for KVM's handling of EFER bits whose behavior is tied to nested SVM.
 *
 * Copyright (C) 2026, Google LLC.
 */
#include "test_util.h"
#include "kvm_util.h"
#include "processor.h"
#include "svm_util.h"
#include "kselftest.h"

static bool l2_ran;

static void l2_clear_efer_svme(void)
{
	u64 efer = rdmsr(MSR_EFER);

	/* generic_svm_setup() initializes EFER_SVME set for L2 */
	GUEST_ASSERT(efer & EFER_SVME);
	wrmsr(MSR_EFER, efer & ~EFER_SVME);

	/* Unreachable, L1 should be shutdown */
	GUEST_ASSERT(0);
}

static void l1_clear_efer_svme(struct svm_test_data *svm)
{
	generic_svm_setup(svm, l2_clear_efer_svme);
	run_guest(svm->vmcb, svm->vmcb_gpa);

	/* Unreachable, L1 should be shutdown */
	GUEST_ASSERT(0);
}

static void l2_lmsle(void)
{
	GUEST_ASSERT(rdmsr(MSR_EFER) & EFER_LMSLE);
	l2_ran = true;
	vmmcall();
}

static void l1_lmsle(struct svm_test_data *svm)
{
	bool lmsle_mbz = this_cpu_has(X86_FEATURE_EFER_LMSLE_MBZ);
	struct vmcb *vmcb = svm->vmcb;
	u64 efer = rdmsr(MSR_EFER);

	/*
	 * Selftests' vCPUs are created with EFER.LMSLE clear; the sub-tests
	 * below need to start from a clean slate.
	 */
	GUEST_ASSERT(!(efer & EFER_LMSLE));
	GUEST_ASSERT(!l2_ran);

	/*
	 * Per the APM, if EFER_LMSLE_MBZ is enumerated in CPUID, "64-bit mode
	 * segment limit checking is not supported and attempting to set
	 * EFER.LMSLE = 1 causes a #GP exception".
	 */
	if (lmsle_mbz) {
		GUEST_ASSERT_EQ(wrmsr_safe(MSR_EFER, efer | EFER_LMSLE), GP_VECTOR);
		GUEST_ASSERT(!(rdmsr(MSR_EFER) & EFER_LMSLE));
	} else {
		GUEST_ASSERT_EQ(wrmsr_safe(MSR_EFER, efer | EFER_LMSLE), 0);
		GUEST_ASSERT(rdmsr(MSR_EFER) & EFER_LMSLE);

		/*
		 * Restore EFER so that generic_svm_setup() doesn't propagate
		 * EFER.LMSLE into vmcb12 on its own, i.e. so that the VMRUN
		 * sub-test actually tests what it thinks it's testing.
		 */
		wrmsr(MSR_EFER, efer);
	}

	/*
	 * VMRUN's consistency checks reject "any MBZ bit of EFER", i.e. a
	 * vmcb12 with EFER.LMSLE set must generate VMEXIT_INVALID when the
	 * defeature is enumerated.
	 */
	generic_svm_setup(svm, l2_lmsle);
	vmcb->save.efer |= EFER_LMSLE;
	run_guest(vmcb, svm->vmcb_gpa);

	if (lmsle_mbz) {
		GUEST_ASSERT_EQ(vmcb->control.exit_code, SVM_EXIT_ERR);
		GUEST_ASSERT(!l2_ran);
	} else {
		GUEST_ASSERT_EQ(vmcb->control.exit_code, SVM_EXIT_VMMCALL);
		GUEST_ASSERT(l2_ran);
		GUEST_ASSERT(vmcb->save.efer & EFER_LMSLE);
	}

	GUEST_DONE();
}

static struct kvm_vcpu *create_l1_vcpu(struct kvm_vm **vm, void *l1_guest_code)
{
	struct kvm_vcpu *vcpu;
	gva_t svm_gva;

	*vm = vm_create_with_one_vcpu(&vcpu, l1_guest_code);

	vcpu_alloc_svm(*vm, &svm_gva);
	vcpu_args_set(vcpu, 1, svm_gva);

	return vcpu;
}

static void test_enumeration(void)
{
	bool lmsle_mbz;

	/*
	 * EFER_LMSLE_MBZ, CPUID.80000008H:EBX[bit 20], is a "defeature" bit,
	 * i.e. is set when the CPU does *not* support long mode segment
	 * limits.  KVM enumerates the defeature if and only if KVM refuses to
	 * set EFER.LMSLE, i.e. if the CPU doesn't support LMSLE, or if KVM
	 * doesn't support nested SVM.  Derive the expectation from raw CPUID
	 * and kvm_amd's "nested" module param rather than from
	 * kvm_cpu_has(X86_FEATURE_SVM), so that the assertion doesn't simply
	 * compare KVM's enumeration to itself.
	 */
	lmsle_mbz = this_cpu_has(X86_FEATURE_EFER_LMSLE_MBZ) ||
		    !this_cpu_has(X86_FEATURE_SVM) ||
		    !kvm_is_nested_virtualization_enabled();

	TEST_ASSERT_EQ(kvm_cpu_has(X86_FEATURE_EFER_LMSLE_MBZ), lmsle_mbz);

	ksft_test_result_pass("KVM enumerates EFER_LMSLE_MBZ=%d\n", lmsle_mbz);
}

static void test_clear_efer_svme(void)
{
	struct kvm_vcpu *vcpu;
	struct kvm_vm *vm;

	vcpu = create_l1_vcpu(&vm, l1_clear_efer_svme);

	vcpu_run(vcpu);
	TEST_ASSERT_KVM_EXIT_REASON(vcpu, KVM_EXIT_SHUTDOWN);

	kvm_vm_free(vm);
	ksft_test_result_pass("L2 clearing EFER.SVME shuts down L1\n");
}

static void test_lmsle(bool lmsle_mbz)
{
	struct kvm_vcpu *vcpu;
	struct kvm_vm *vm;
	struct ucall uc;

	vcpu = create_l1_vcpu(&vm, l1_lmsle);

	vcpu_set_or_clear_cpuid_feature(vcpu, X86_FEATURE_EFER_LMSLE_MBZ,
					lmsle_mbz);

	vcpu_run(vcpu);
	TEST_ASSERT_KVM_EXIT_REASON(vcpu, KVM_EXIT_IO);

	switch (get_ucall(vcpu, &uc)) {
	case UCALL_ABORT:
		REPORT_GUEST_ASSERT(uc);
	case UCALL_DONE:
		break;
	default:
		TEST_FAIL("Unexpected ucall: %lu", uc.cmd);
	}

	kvm_vm_free(vm);
	ksft_test_result_pass("Guest EFER_LMSLE_MBZ=%d\n", lmsle_mbz);
}

static void test_host_initiated_lmsle(void)
{
	struct kvm_vcpu *vcpu;
	struct kvm_vm *vm;
	u64 efer;

	vm = vm_create_with_one_vcpu(&vcpu, NULL);
	vcpu_set_cpuid_feature(vcpu, X86_FEATURE_EFER_LMSLE_MBZ);

	/*
	 * EFER_LMSLE_MBZ is a guest CPUID consistency check, not a host
	 * capability, i.e. must not be enforced against host-initiated writes,
	 * so that userspace can set MSRs before it sets guest CPUID.
	 */
	efer = vcpu_get_msr(vcpu, MSR_EFER);
	TEST_ASSERT(!(efer & EFER_LMSLE), "EFER.LMSLE unexpectedly set");

	vcpu_set_msr(vcpu, MSR_EFER, efer | EFER_LMSLE);
	TEST_ASSERT_EQ(vcpu_get_msr(vcpu, MSR_EFER), efer | EFER_LMSLE);

	kvm_vm_free(vm);
	ksft_test_result_pass("Host-initiated EFER.LMSLE=1 is allowed\n");
}

static void test_sregs_lmsle(void)
{
	struct kvm_vcpu *vcpu;
	struct kvm_sregs sregs;
	struct kvm_vm *vm;
	int rc;

	vm = vm_create_with_one_vcpu(&vcpu, NULL);
	vcpu_set_cpuid_feature(vcpu, X86_FEATURE_EFER_LMSLE_MBZ);

	/*
	 * Unlike KVM_SET_MSRS, KVM_SET_SREGS runs the full set of guest CPUID
	 * checks, i.e. rejects EFER.LMSLE even though it's host-initiated.
	 */
	vcpu_sregs_get(vcpu, &sregs);
	TEST_ASSERT(!(sregs.efer & EFER_LMSLE), "EFER.LMSLE unexpectedly set");

	sregs.efer |= EFER_LMSLE;
	rc = _vcpu_sregs_set(vcpu, &sregs);
	TEST_ASSERT(rc, "KVM allowed EFER.LMSLE with EFER_LMSLE_MBZ set");

	kvm_vm_free(vm);
	ksft_test_result_pass("KVM_SET_SREGS rejects EFER.LMSLE=1\n");
}

int main(int argc, char *argv[])
{
	bool has_nested_svm, has_lmsle;

	ksft_print_header();
	ksft_set_plan(6);

	test_enumeration();

	/*
	 * The sub-tests below need to actually run a nested guest, and the
	 * EFER.LMSLE sub-tests additionally need KVM to allow EFER.LMSLE.
	 * It's KVM's view of the world, not raw CPUID, that dictates whether
	 * EFER.LMSLE is allowed, i.e. whether the defeature is emulated.
	 */
	has_nested_svm = kvm_cpu_has(X86_FEATURE_SVM);
	has_lmsle = has_nested_svm &&
		    !kvm_cpu_has(X86_FEATURE_EFER_LMSLE_MBZ);

	if (!has_nested_svm)
		ksft_print_msg("Nested SVM unsupported\n");
	else if (!has_lmsle)
		ksft_print_msg("KVM doesn't support EFER.LMSLE\n");

	if (has_nested_svm) {
		test_clear_efer_svme();
		test_lmsle(true);
	} else {
		ksft_test_result_skip("L2 clearing EFER.SVME shuts down L1\n");
		ksft_test_result_skip("Guest EFER_LMSLE_MBZ=1\n");
	}

	if (has_lmsle) {
		test_lmsle(false);
		test_host_initiated_lmsle();
		test_sregs_lmsle();
	} else {
		ksft_test_result_skip("Guest EFER_LMSLE_MBZ=0\n");
		ksft_test_result_skip("Host-initiated EFER.LMSLE=1 is allowed\n");
		ksft_test_result_skip("KVM_SET_SREGS rejects EFER.LMSLE=1\n");
	}

	ksft_finished();
}
