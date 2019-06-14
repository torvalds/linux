/*
 * vmx_set_nested_state_test
 *
 * Copyright (C) 2019, Google LLC.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 *
 * This test verifies the integrity of calling the ioctl KVM_SET_NESTED_STATE.
 */

#include "test_util.h"
#include "kvm_util.h"
#include "processor.h"
#include "vmx.h"

#include <errno.h>
#include <linux/kvm.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

/*
 * Mirror of VMCS12_REVISION in arch/x86/kvm/vmx/vmcs12.h. If that value
 * changes this should be updated.
 */
#define VMCS12_REVISION 0x11e57ed0
#define VCPU_ID 5

void test_nested_state(struct kvm_vm *vm, struct kvm_nested_state *state)
{
	volatile struct kvm_run *run;

	vcpu_nested_state_set(vm, VCPU_ID, state, false);
	run = vcpu_state(vm, VCPU_ID);
	vcpu_run(vm, VCPU_ID);
	TEST_ASSERT(run->exit_reason == KVM_EXIT_SHUTDOWN,
		"Got exit_reason other than KVM_EXIT_SHUTDOWN: %u (%s),\n",
		run->exit_reason,
		exit_reason_str(run->exit_reason));
}

void test_nested_state_expect_errno(struct kvm_vm *vm,
				    struct kvm_nested_state *state,
				    int expected_errno)
{
	volatile struct kvm_run *run;
	int rv;

	rv = vcpu_nested_state_set(vm, VCPU_ID, state, true);
	TEST_ASSERT(rv == -1 && errno == expected_errno,
		"Expected %s (%d) from vcpu_nested_state_set but got rv: %i errno: %s (%d)",
		strerror(expected_errno), expected_errno, rv, strerror(errno),
		errno);
	run = vcpu_state(vm, VCPU_ID);
	vcpu_run(vm, VCPU_ID);
	TEST_ASSERT(run->exit_reason == KVM_EXIT_SHUTDOWN,
		"Got exit_reason other than KVM_EXIT_SHUTDOWN: %u (%s),\n",
		run->exit_reason,
		exit_reason_str(run->exit_reason));
}

void test_nested_state_expect_einval(struct kvm_vm *vm,
				     struct kvm_nested_state *state)
{
	test_nested_state_expect_errno(vm, state, EINVAL);
}

void test_nested_state_expect_efault(struct kvm_vm *vm,
				     struct kvm_nested_state *state)
{
	test_nested_state_expect_errno(vm, state, EFAULT);
}

void set_revision_id_for_vmcs12(struct kvm_nested_state *state,
				u32 vmcs12_revision)
{
	/* Set revision_id in vmcs12 to vmcs12_revision. */
	memcpy(state->data, &vmcs12_revision, sizeof(u32));
}

void set_default_state(struct kvm_nested_state *state)
{
	memset(state, 0, sizeof(*state));
	state->flags = KVM_STATE_NESTED_RUN_PENDING |
		       KVM_STATE_NESTED_GUEST_MODE;
	state->format = 0;
	state->size = sizeof(*state);
}

void set_default_vmx_state(struct kvm_nested_state *state, int size)
{
	memset(state, 0, size);
	state->flags = KVM_STATE_NESTED_GUEST_MODE  |
			KVM_STATE_NESTED_RUN_PENDING |
			KVM_STATE_NESTED_EVMCS;
	state->format = 0;
	state->size = size;
	state->vmx.vmxon_pa = 0x1000;
	state->vmx.vmcs_pa = 0x2000;
	state->vmx.smm.flags = 0;
	set_revision_id_for_vmcs12(state, VMCS12_REVISION);
}

void test_vmx_nested_state(struct kvm_vm *vm)
{
	/* Add a page for VMCS12. */
	const int state_sz = sizeof(struct kvm_nested_state) + getpagesize();
	struct kvm_nested_state *state =
		(struct kvm_nested_state *)malloc(state_sz);

	/* The format must be set to 0. 0 for VMX, 1 for SVM. */
	set_default_vmx_state(state, state_sz);
	state->format = 1;
	test_nested_state_expect_einval(vm, state);

	/*
	 * We cannot virtualize anything if the guest does not have VMX
	 * enabled.
	 */
	set_default_vmx_state(state, state_sz);
	test_nested_state_expect_einval(vm, state);

	/*
	 * We cannot virtualize anything if the guest does not have VMX
	 * enabled.  We expect KVM_SET_NESTED_STATE to return 0 if vmxon_pa
	 * is set to -1ull.
	 */
	set_default_vmx_state(state, state_sz);
	state->vmx.vmxon_pa = -1ull;
	test_nested_state(vm, state);

	/* Enable VMX in the guest CPUID. */
	vcpu_set_cpuid(vm, VCPU_ID, kvm_get_supported_cpuid());

	/* It is invalid to have vmxon_pa == -1ull and SMM flags non-zero. */
	set_default_vmx_state(state, state_sz);
	state->vmx.vmxon_pa = -1ull;
	state->vmx.smm.flags = 1;
	test_nested_state_expect_einval(vm, state);

	/* It is invalid to have vmxon_pa == -1ull and vmcs_pa != -1ull. */
	set_default_vmx_state(state, state_sz);
	state->vmx.vmxon_pa = -1ull;
	state->vmx.vmcs_pa = 0;
	test_nested_state_expect_einval(vm, state);

	/*
	 * Setting vmxon_pa == -1ull and vmcs_pa == -1ull exits early without
	 * setting the nested state.
	 */
	set_default_vmx_state(state, state_sz);
	state->vmx.vmxon_pa = -1ull;
	state->vmx.vmcs_pa = -1ull;
	test_nested_state(vm, state);

	/* It is invalid to have vmxon_pa set to a non-page aligned address. */
	set_default_vmx_state(state, state_sz);
	state->vmx.vmxon_pa = 1;
	test_nested_state_expect_einval(vm, state);

	/*
	 * It is invalid to have KVM_STATE_NESTED_SMM_GUEST_MODE and
	 * KVM_STATE_NESTED_GUEST_MODE set together.
	 */
	set_default_vmx_state(state, state_sz);
	state->flags = KVM_STATE_NESTED_GUEST_MODE  |
		      KVM_STATE_NESTED_RUN_PENDING;
	state->vmx.smm.flags = KVM_STATE_NESTED_SMM_GUEST_MODE;
	test_nested_state_expect_einval(vm, state);

	/*
	 * It is invalid to have any of the SMM flags set besides:
	 *	KVM_STATE_NESTED_SMM_GUEST_MODE
	 *	KVM_STATE_NESTED_SMM_VMXON
	 */
	set_default_vmx_state(state, state_sz);
	state->vmx.smm.flags = ~(KVM_STATE_NESTED_SMM_GUEST_MODE |
				KVM_STATE_NESTED_SMM_VMXON);
	test_nested_state_expect_einval(vm, state);

	/* Outside SMM, SMM flags must be zero. */
	set_default_vmx_state(state, state_sz);
	state->flags = 0;
	state->vmx.smm.flags = KVM_STATE_NESTED_SMM_GUEST_MODE;
	test_nested_state_expect_einval(vm, state);

	/* Size must be large enough to fit kvm_nested_state and vmcs12. */
	set_default_vmx_state(state, state_sz);
	state->size = sizeof(*state);
	test_nested_state(vm, state);

	/* vmxon_pa cannot be the same address as vmcs_pa. */
	set_default_vmx_state(state, state_sz);
	state->vmx.vmxon_pa = 0;
	state->vmx.vmcs_pa = 0;
	test_nested_state_expect_einval(vm, state);

	/* The revision id for vmcs12 must be VMCS12_REVISION. */
	set_default_vmx_state(state, state_sz);
	set_revision_id_for_vmcs12(state, 0);
	test_nested_state_expect_einval(vm, state);

	/*
	 * Test that if we leave nesting the state reflects that when we get
	 * it again.
	 */
	set_default_vmx_state(state, state_sz);
	state->vmx.vmxon_pa = -1ull;
	state->vmx.vmcs_pa = -1ull;
	state->flags = 0;
	test_nested_state(vm, state);
	vcpu_nested_state_get(vm, VCPU_ID, state);
	TEST_ASSERT(state->size >= sizeof(*state) && state->size <= state_sz,
		    "Size must be between %d and %d.  The size returned was %d.",
		    sizeof(*state), state_sz, state->size);
	TEST_ASSERT(state->vmx.vmxon_pa == -1ull, "vmxon_pa must be -1ull.");
	TEST_ASSERT(state->vmx.vmcs_pa == -1ull, "vmcs_pa must be -1ull.");

	free(state);
}

int main(int argc, char *argv[])
{
	struct kvm_vm *vm;
	struct kvm_nested_state state;
	struct kvm_cpuid_entry2 *entry = kvm_get_supported_cpuid_entry(1);

	if (!kvm_check_cap(KVM_CAP_NESTED_STATE)) {
		printf("KVM_CAP_NESTED_STATE not available, skipping test\n");
		exit(KSFT_SKIP);
	}

	/*
	 * AMD currently does not implement set_nested_state, so for now we
	 * just early out.
	 */
	if (!(entry->ecx & CPUID_VMX)) {
		fprintf(stderr, "nested VMX not enabled, skipping test\n");
		exit(KSFT_SKIP);
	}

	vm = vm_create_default(VCPU_ID, 0, 0);

	/* Passing a NULL kvm_nested_state causes a EFAULT. */
	test_nested_state_expect_efault(vm, NULL);

	/* 'size' cannot be smaller than sizeof(kvm_nested_state). */
	set_default_state(&state);
	state.size = 0;
	test_nested_state_expect_einval(vm, &state);

	/*
	 * Setting the flags 0xf fails the flags check.  The only flags that
	 * can be used are:
	 *     KVM_STATE_NESTED_GUEST_MODE
	 *     KVM_STATE_NESTED_RUN_PENDING
	 *     KVM_STATE_NESTED_EVMCS
	 */
	set_default_state(&state);
	state.flags = 0xf;
	test_nested_state_expect_einval(vm, &state);

	/*
	 * If KVM_STATE_NESTED_RUN_PENDING is set then
	 * KVM_STATE_NESTED_GUEST_MODE has to be set as well.
	 */
	set_default_state(&state);
	state.flags = KVM_STATE_NESTED_RUN_PENDING;
	test_nested_state_expect_einval(vm, &state);

	/*
	 * TODO: When SVM support is added for KVM_SET_NESTED_STATE
	 *       add tests here to support it like VMX.
	 */
	if (entry->ecx & CPUID_VMX)
		test_vmx_nested_state(vm);

	kvm_vm_free(vm);
	return 0;
}
