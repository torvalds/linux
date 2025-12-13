// SPDX-License-Identifier: GPL-2.0-only
/*
 * smccc_filter - Tests for the SMCCC filter UAPI.
 *
 * Copyright (c) 2023 Google LLC
 *
 * This test includes:
 *  - Tests that the UAPI constraints are upheld by KVM. For example, userspace
 *    is prevented from filtering the architecture range of SMCCC calls.
 *  - Test that the filter actions (DENIED, FWD_TO_USER) work as intended.
 */

#include <linux/arm-smccc.h>
#include <linux/psci.h>
#include <stdint.h>

#include "processor.h"
#include "test_util.h"

enum smccc_conduit {
	HVC_INSN,
	SMC_INSN,
};

static bool test_runs_at_el2(void)
{
	struct kvm_vm *vm = vm_create(1);
	struct kvm_vcpu_init init;

	kvm_get_default_vcpu_target(vm, &init);
	kvm_vm_free(vm);

	return init.features[0] & BIT(KVM_ARM_VCPU_HAS_EL2);
}

#define for_each_conduit(conduit)					\
	for (conduit = test_runs_at_el2() ? SMC_INSN : HVC_INSN;	\
	     conduit <= SMC_INSN; conduit++)

static void guest_main(uint32_t func_id, enum smccc_conduit conduit)
{
	struct arm_smccc_res res;

	if (conduit == SMC_INSN)
		smccc_smc(func_id, 0, 0, 0, 0, 0, 0, 0, &res);
	else
		smccc_hvc(func_id, 0, 0, 0, 0, 0, 0, 0, &res);

	GUEST_SYNC(res.a0);
}

static int __set_smccc_filter(struct kvm_vm *vm, uint32_t start, uint32_t nr_functions,
			      enum kvm_smccc_filter_action action)
{
	struct kvm_smccc_filter filter = {
		.base		= start,
		.nr_functions	= nr_functions,
		.action		= action,
	};

	return __kvm_device_attr_set(vm->fd, KVM_ARM_VM_SMCCC_CTRL,
				     KVM_ARM_VM_SMCCC_FILTER, &filter);
}

static void set_smccc_filter(struct kvm_vm *vm, uint32_t start, uint32_t nr_functions,
			     enum kvm_smccc_filter_action action)
{
	int ret = __set_smccc_filter(vm, start, nr_functions, action);

	TEST_ASSERT(!ret, "failed to configure SMCCC filter: %d", ret);
}

static struct kvm_vm *setup_vm(struct kvm_vcpu **vcpu)
{
	struct kvm_vcpu_init init;
	struct kvm_vm *vm;

	vm = vm_create(1);
	kvm_get_default_vcpu_target(vm, &init);

	/*
	 * Enable in-kernel emulation of PSCI to ensure that calls are denied
	 * due to the SMCCC filter, not because of KVM.
	 */
	init.features[0] |= (1 << KVM_ARM_VCPU_PSCI_0_2);

	*vcpu = aarch64_vcpu_add(vm, 0, &init, guest_main);
	kvm_arch_vm_finalize_vcpus(vm);
	return vm;
}

static void test_pad_must_be_zero(void)
{
	struct kvm_vcpu *vcpu;
	struct kvm_vm *vm = setup_vm(&vcpu);
	struct kvm_smccc_filter filter = {
		.base		= PSCI_0_2_FN_PSCI_VERSION,
		.nr_functions	= 1,
		.action		= KVM_SMCCC_FILTER_DENY,
		.pad		= { -1 },
	};
	int r;

	r = __kvm_device_attr_set(vm->fd, KVM_ARM_VM_SMCCC_CTRL,
				  KVM_ARM_VM_SMCCC_FILTER, &filter);
	TEST_ASSERT(r < 0 && errno == EINVAL,
		    "Setting filter with nonzero padding should return EINVAL");
}

/* Ensure that userspace cannot filter the Arm Architecture SMCCC range */
static void test_filter_reserved_range(void)
{
	struct kvm_vcpu *vcpu;
	struct kvm_vm *vm = setup_vm(&vcpu);
	uint32_t smc64_fn;
	int r;

	r = __set_smccc_filter(vm, ARM_SMCCC_ARCH_WORKAROUND_1,
			       1, KVM_SMCCC_FILTER_DENY);
	TEST_ASSERT(r < 0 && errno == EEXIST,
		    "Attempt to filter reserved range should return EEXIST");

	smc64_fn = ARM_SMCCC_CALL_VAL(ARM_SMCCC_FAST_CALL, ARM_SMCCC_SMC_64,
				      0, 0);

	r = __set_smccc_filter(vm, smc64_fn, 1, KVM_SMCCC_FILTER_DENY);
	TEST_ASSERT(r < 0 && errno == EEXIST,
		    "Attempt to filter reserved range should return EEXIST");

	kvm_vm_free(vm);
}

static void test_invalid_nr_functions(void)
{
	struct kvm_vcpu *vcpu;
	struct kvm_vm *vm = setup_vm(&vcpu);
	int r;

	r = __set_smccc_filter(vm, PSCI_0_2_FN64_CPU_ON, 0, KVM_SMCCC_FILTER_DENY);
	TEST_ASSERT(r < 0 && errno == EINVAL,
		    "Attempt to filter 0 functions should return EINVAL");

	kvm_vm_free(vm);
}

static void test_overflow_nr_functions(void)
{
	struct kvm_vcpu *vcpu;
	struct kvm_vm *vm = setup_vm(&vcpu);
	int r;

	r = __set_smccc_filter(vm, ~0, ~0, KVM_SMCCC_FILTER_DENY);
	TEST_ASSERT(r < 0 && errno == EINVAL,
		    "Attempt to overflow filter range should return EINVAL");

	kvm_vm_free(vm);
}

static void test_reserved_action(void)
{
	struct kvm_vcpu *vcpu;
	struct kvm_vm *vm = setup_vm(&vcpu);
	int r;

	r = __set_smccc_filter(vm, PSCI_0_2_FN64_CPU_ON, 1, -1);
	TEST_ASSERT(r < 0 && errno == EINVAL,
		    "Attempt to use reserved filter action should return EINVAL");

	kvm_vm_free(vm);
}


/* Test that overlapping configurations of the SMCCC filter are rejected */
static void test_filter_overlap(void)
{
	struct kvm_vcpu *vcpu;
	struct kvm_vm *vm = setup_vm(&vcpu);
	int r;

	set_smccc_filter(vm, PSCI_0_2_FN64_CPU_ON, 1, KVM_SMCCC_FILTER_DENY);

	r = __set_smccc_filter(vm, PSCI_0_2_FN64_CPU_ON, 1, KVM_SMCCC_FILTER_DENY);
	TEST_ASSERT(r < 0 && errno == EEXIST,
		    "Attempt to filter already configured range should return EEXIST");

	kvm_vm_free(vm);
}

static void expect_call_denied(struct kvm_vcpu *vcpu)
{
	struct ucall uc;

	if (get_ucall(vcpu, &uc) != UCALL_SYNC)
		TEST_FAIL("Unexpected ucall: %lu", uc.cmd);

	TEST_ASSERT(uc.args[1] == SMCCC_RET_NOT_SUPPORTED,
		    "Unexpected SMCCC return code: %lu", uc.args[1]);
}

/* Denied SMCCC calls have a return code of SMCCC_RET_NOT_SUPPORTED */
static void test_filter_denied(void)
{
	enum smccc_conduit conduit;
	struct kvm_vcpu *vcpu;
	struct kvm_vm *vm;

	for_each_conduit(conduit) {
		vm = setup_vm(&vcpu);

		set_smccc_filter(vm, PSCI_0_2_FN_PSCI_VERSION, 1, KVM_SMCCC_FILTER_DENY);
		vcpu_args_set(vcpu, 2, PSCI_0_2_FN_PSCI_VERSION, conduit);

		vcpu_run(vcpu);
		expect_call_denied(vcpu);

		kvm_vm_free(vm);
	}
}

static void expect_call_fwd_to_user(struct kvm_vcpu *vcpu, uint32_t func_id,
				    enum smccc_conduit conduit)
{
	struct kvm_run *run = vcpu->run;

	TEST_ASSERT(run->exit_reason == KVM_EXIT_HYPERCALL,
		    "Unexpected exit reason: %u", run->exit_reason);
	TEST_ASSERT(run->hypercall.nr == func_id,
		    "Unexpected SMCCC function: %llu", run->hypercall.nr);

	if (conduit == SMC_INSN)
		TEST_ASSERT(run->hypercall.flags & KVM_HYPERCALL_EXIT_SMC,
			    "KVM_HYPERCALL_EXIT_SMC is not set");
	else
		TEST_ASSERT(!(run->hypercall.flags & KVM_HYPERCALL_EXIT_SMC),
			    "KVM_HYPERCALL_EXIT_SMC is set");
}

/* SMCCC calls forwarded to userspace cause KVM_EXIT_HYPERCALL exits */
static void test_filter_fwd_to_user(void)
{
	enum smccc_conduit conduit;
	struct kvm_vcpu *vcpu;
	struct kvm_vm *vm;

	for_each_conduit(conduit) {
		vm = setup_vm(&vcpu);

		set_smccc_filter(vm, PSCI_0_2_FN_PSCI_VERSION, 1, KVM_SMCCC_FILTER_FWD_TO_USER);
		vcpu_args_set(vcpu, 2, PSCI_0_2_FN_PSCI_VERSION, conduit);

		vcpu_run(vcpu);
		expect_call_fwd_to_user(vcpu, PSCI_0_2_FN_PSCI_VERSION, conduit);

		kvm_vm_free(vm);
	}
}

static bool kvm_supports_smccc_filter(void)
{
	struct kvm_vm *vm = vm_create_barebones();
	int r;

	r = __kvm_has_device_attr(vm->fd, KVM_ARM_VM_SMCCC_CTRL, KVM_ARM_VM_SMCCC_FILTER);

	kvm_vm_free(vm);
	return !r;
}

int main(void)
{
	TEST_REQUIRE(kvm_supports_smccc_filter());

	test_pad_must_be_zero();
	test_invalid_nr_functions();
	test_overflow_nr_functions();
	test_reserved_action();
	test_filter_reserved_range();
	test_filter_overlap();
	test_filter_denied();
	test_filter_fwd_to_user();
}
