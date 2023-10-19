// SPDX-License-Identifier: GPL-2.0-only
/*
 * maximum APIC ID capability tests
 *
 * Copyright (C) 2022, Intel, Inc.
 *
 * Tests for getting/setting maximum APIC ID capability
 */

#include "kvm_util.h"

#define MAX_VCPU_ID	2

int main(int argc, char *argv[])
{
	struct kvm_vm *vm;
	int ret;

	vm = vm_create_barebones();

	/* Get KVM_CAP_MAX_VCPU_ID cap supported in KVM */
	ret = vm_check_cap(vm, KVM_CAP_MAX_VCPU_ID);

	/* Try to set KVM_CAP_MAX_VCPU_ID beyond KVM cap */
	ret = __vm_enable_cap(vm, KVM_CAP_MAX_VCPU_ID, ret + 1);
	TEST_ASSERT(ret < 0,
		    "Setting KVM_CAP_MAX_VCPU_ID beyond KVM cap should fail");

	/* Set KVM_CAP_MAX_VCPU_ID */
	vm_enable_cap(vm, KVM_CAP_MAX_VCPU_ID, MAX_VCPU_ID);


	/* Try to set KVM_CAP_MAX_VCPU_ID again */
	ret = __vm_enable_cap(vm, KVM_CAP_MAX_VCPU_ID, MAX_VCPU_ID + 1);
	TEST_ASSERT(ret < 0,
		    "Setting KVM_CAP_MAX_VCPU_ID multiple times should fail");

	/* Create vCPU with id beyond KVM_CAP_MAX_VCPU_ID cap*/
	ret = __vm_ioctl(vm, KVM_CREATE_VCPU, (void *)MAX_VCPU_ID);
	TEST_ASSERT(ret < 0, "Creating vCPU with ID > MAX_VCPU_ID should fail");

	kvm_vm_free(vm);
	return 0;
}
