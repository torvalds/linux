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

	/* Test BOOT_CPU_ID interaction (MAX_VCPU_ID cannot be lower) */
	if (kvm_has_cap(KVM_CAP_SET_BOOT_CPU_ID)) {
		vm_ioctl(vm, KVM_SET_BOOT_CPU_ID, (void *)MAX_VCPU_ID);

		/* Try setting KVM_CAP_MAX_VCPU_ID below BOOT_CPU_ID */
		ret = __vm_enable_cap(vm, KVM_CAP_MAX_VCPU_ID, MAX_VCPU_ID - 1);
		TEST_ASSERT(ret < 0,
			    "Setting KVM_CAP_MAX_VCPU_ID below BOOT_CPU_ID should fail");
	}

	/* Set KVM_CAP_MAX_VCPU_ID */
	vm_enable_cap(vm, KVM_CAP_MAX_VCPU_ID, MAX_VCPU_ID);

	/* Try to set KVM_CAP_MAX_VCPU_ID again */
	ret = __vm_enable_cap(vm, KVM_CAP_MAX_VCPU_ID, MAX_VCPU_ID + 1);
	TEST_ASSERT(ret < 0,
		    "Setting KVM_CAP_MAX_VCPU_ID multiple times should fail");

	/* Create vCPU with id beyond KVM_CAP_MAX_VCPU_ID cap */
	ret = __vm_ioctl(vm, KVM_CREATE_VCPU, (void *)MAX_VCPU_ID);
	TEST_ASSERT(ret < 0, "Creating vCPU with ID > MAX_VCPU_ID should fail");

	/* Create vCPU with bits 63:32 != 0, but an otherwise valid id */
	ret = __vm_ioctl(vm, KVM_CREATE_VCPU, (void *)(1L << 32));
	TEST_ASSERT(ret < 0, "Creating vCPU with ID[63:32] != 0 should fail");

	/* Create vCPU with id within bounds */
	ret = __vm_ioctl(vm, KVM_CREATE_VCPU, (void *)0);
	TEST_ASSERT(ret >= 0, "Creating vCPU with ID 0 should succeed");

	close(ret);
	kvm_vm_free(vm);
	return 0;
}
