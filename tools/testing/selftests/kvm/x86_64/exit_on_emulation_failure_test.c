// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2022, Google LLC.
 *
 * Test for KVM_CAP_EXIT_ON_EMULATION_FAILURE.
 */

#define _GNU_SOURCE /* for program_invocation_short_name */

#include "flds_emulation.h"

#include "test_util.h"

#define MMIO_GPA	0x700000000
#define MMIO_GVA	MMIO_GPA

static void guest_code(void)
{
	/* Execute flds with an MMIO address to force KVM to emulate it. */
	flds(MMIO_GVA);
	GUEST_DONE();
}

int main(int argc, char *argv[])
{
	struct kvm_vcpu *vcpu;
	struct kvm_vm *vm;

	TEST_REQUIRE(kvm_has_cap(KVM_CAP_EXIT_ON_EMULATION_FAILURE));

	vm = vm_create_with_one_vcpu(&vcpu, guest_code);
	vm_enable_cap(vm, KVM_CAP_EXIT_ON_EMULATION_FAILURE, 1);
	virt_map(vm, MMIO_GVA, MMIO_GPA, 1);

	vcpu_run(vcpu);
	handle_flds_emulation_failure_exit(vcpu);
	vcpu_run(vcpu);
	ASSERT_EQ(get_ucall(vcpu, NULL), UCALL_DONE);

	kvm_vm_free(vm);
	return 0;
}
